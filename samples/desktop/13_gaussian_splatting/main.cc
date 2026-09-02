/* -------------------------------------------------------------------------- */
//
//    13 - Gaussian Splatting
//
//  Where we go beyond the triangle.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "miniply.h"

namespace shader_interop {
#include "shaders/interop.h"
#include "shaders/radix_interop.h"
}

// ----------------------------------------------------------------------------

class SampleApp final : public Application {
 public:
  static constexpr bool kEnableDebugRun{ false };
  static constexpr uint32_t kDebugBufferSize{ 281 /*1157141*/ };

  static constexpr uint32_t kHeuristicMaxTilePerGaussian{ 8 }; //

  public:
    enum GSCompute {
      GSCompute_Preprocess,
      GSCompute_DecoupledPrefixSum,
      GSCompute_ResetIndirectBuffers,
      GSCompute_DuplicateKeys,

      GSCompute_kCount,
    };

    enum RadixCompute {
      RadixCompute_Histogram,
      RadixCompute_PrefixSum,
      RadixCompute_Binning,

      RadixCompute_kCount
    };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.sample_count = VK_SAMPLE_COUNT_8_BIT;
    return S;
  }

  bool setup() final {
    wm_->set_title("13 - Gaussian Splatting");

    renderer_.set_clear_color({ 0.2f, 0.75f, 0.5f, 1.0f });

    /* Setup the ArcBall camera. */
    {
      camera_.makePerspective(
        lina::radians(60.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.1f,
        750.0f
      );
      camera_.set_controller(&arcball_controller_);
      arcball_controller_.set_dolly(55.0f);
    }

    /* Allocate the uniform buffer. */
    {
      // TODO: allocate as a properly padded ring buffer

      uniform_buffer_ = context_.createBuffer(
        sizeof(host_data_), // (* max_frames_in_flight)
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        , VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Import point cloud (ply) data */
    std::vector<shader_interop::GaussianData> gaussians{};
    {
      auto reader = miniply::PLYReader(
        ASSETS_DIR "pointclouds/bonzai_7000/point_cloud.ply"
      );
      if (!reader.valid()) {
        LOGW("miniply: invalid filename");
        return false;
      }

      auto const vertex_idx = reader.find_element("vertex");
      if (vertex_idx == miniply::kInvalidIndex) {
        LOGW("miniply: element not found");
        return false;
      }
      if (!reader.load_element()) {
        LOGW("miniply: load element fails");
        return false;
      }

      // ---

      gaussians_count_ = static_cast<uint32_t>(reader.num_rows());
      gaussians.resize(gaussians_count_);

      constexpr uint32_t kPropCount = 14u;
      constexpr std::array<const char*, kPropCount> names{
        "x", "y", "z",
        "rot_1", "rot_2", "rot_3", "rot_0", // 'w' at the end
        "scale_0", "scale_1", "scale_2",
        "f_dc_0", "f_dc_1", "f_dc_2", "opacity"
      };

      std::array<uint32_t, kPropCount> indexes{};
      reader.find_properties(indexes.data(), kPropCount, names.data());

      auto const stride = sizeof(shader_interop::GaussianData);

      // Extract properties individually to count for paddings.
      reader.extract_properties_with_stride(
        &indexes[0], 3, miniply::PLYPropertyType::Float,
        &gaussians[0].position[0], stride
      );
      reader.extract_properties_with_stride(
        &indexes[3], 4, miniply::PLYPropertyType::Float,
        &gaussians[0].rotation[0], stride
      );
      reader.extract_properties_with_stride(
        &indexes[7], 3, miniply::PLYPropertyType::Float,
        &gaussians[0].scale[0], stride
      );
      reader.extract_properties_with_stride(
        &indexes[10], 4, miniply::PLYPropertyType::Float,
        &gaussians[0].color[0], stride
      );
    }

    // [ later on, would be better to allocate large buffer we subaddress ]

    if constexpr(kEnableDebugRun)
    {
      LOGW(">>>> DEBUG_RUN is ON <<<<");
      gaussians_count_ = kDebugBufferSize; //
    }

    /* Allocate device buffers. */
    {
      gaussian_sbo_ = context_.transientCreateBuffer(
        gaussians,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      );

      splat_sbo_ = context_.createBuffer(
        gaussians_count_ * sizeof(shader_interop::SplatOutput),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      );

      if constexpr (kEnableDebugRun)
      {
        splat_kv_heuristic_size_ = gaussians_count_;

        splat_keys_sbo_ = context_.createBuffer(
          2u * splat_kv_heuristic_size_ * sizeof(uint64_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
          , VMA_MEMORY_USAGE_GPU_TO_CPU
        );

        uint64_t *buffer;
        context_.mapMemory(splat_keys_sbo_, &buffer);
          std::srand(std::time({}));
          for (size_t i = 0; i < splat_kv_heuristic_size_; ++i) {
            buffer[i] = rand();
          }
        context_.unmapMemory(splat_keys_sbo_);
      }
      else
      {
        // As we don't know how many total tiles would be touched
        // we must allocated a maximum buffer based off an heuristic factor.
        splat_kv_heuristic_size_ = gaussians_count_
                                 * kHeuristicMaxTilePerGaussian;

        // Double buffered for ping-ponging.
        splat_keys_sbo_ = context_.createBuffer(
          2u * splat_kv_heuristic_size_ * sizeof(uint64_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        );
      }

      splat_values_sbo_ = context_.createBuffer(
        2u * splat_kv_heuristic_size_ * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      );
    }

    /* PrefixSum buffers. */
    {
      if constexpr(kEnableDebugRun)
      {
        std::vector<uint32_t> counts(gaussians_count_, 0);
        for (size_t i = 0; (i<kDebugBufferSize) && (i<counts.size()); ++i) {
          counts[i] = 1;
        }

        splat_tilecount_sbo_ = context_.transientCreateBuffer(
          counts,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        );
      }
      else
      {
        splat_tilecount_sbo_ = context_.createBuffer(
          gaussians_count_ * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
          // , VMA_MEMORY_USAGE_GPU_TO_CPU         // DEBUG
        );
      }

      prefix_output_sbo_ = context_.createBuffer(
        gaussians_count_ * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        // , VMA_MEMORY_USAGE_GPU_TO_CPU         // DEBUG
      );

      // --------------------------------------

      // Hold 1 atomic counter + descriptor flags.
      uint32_t const prefixDescriptorBufferSize = 1u + 3u * vk_utils::GetKernelGridDim(
        gaussians_count_,
        shader_interop::kCompute_PrefixSum_kernelSize_x
      );

      prefix_descriptor_sbo_ = context_.createBuffer(
        prefixDescriptorBufferSize * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      );

      // - The 3 first uint32_t are X,Y,Z gridDim for the radix histogram.
      // - the 4th uint32_t is the total size of keys (prefixSum total).
      // - the 5th, 6th, 7th are X,Y,Z gridDim for the radix digit binning.
      indirect_kv_count_sbo_ = context_.createBuffer(
        8u * sizeof(uint32_t), //
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
        , VMA_MEMORY_USAGE_GPU_TO_CPU         // DEBUG
      );
      key_count_offset_           = 3u * sizeof(uint32_t);
      indirect_histogram_offset_  = 0u;
      indirect_binning_offset_    = 4u * sizeof(uint32_t);

      // if we bypass the gaussian pipeline / prefix sum
      // we need to specify the (debug) indirect buffer directly.
      if constexpr (kEnableDebugRun) {
        uint32_t *buffer;
        context_.mapMemory(indirect_kv_count_sbo_, &buffer);
          buffer[0] = vk_utils::GetKernelGridDim(kDebugBufferSize, shader_interop::kTileSize);
          buffer[1] = 1;
          buffer[2] = 1;
          buffer[3] = kDebugBufferSize;
          buffer[4] = vk_utils::GetKernelGridDim(kDebugBufferSize, shader_interop::kRadixSize);
          buffer[5] = 1;
          buffer[6] = 1;
        context_.unmapMemory(indirect_kv_count_sbo_);
      }
    }

    /* Create the Compute Pipelines */
    {
      pipeline_layout_ = context_.createPipelineLayout({
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(push_constant_),
          }
        },
      });

      auto shaders = context_.createShaderModules(SAMPLE_SPIRV_DIR, {
        "gs_preprocess.slang",
        "gs_prefix_sum.slang",
        "gs_reset_indirect.slang",
        "gs_duplicate_keys.slang",
      });
      context_.createComputePipelines(
        pipeline_layout_,
        shaders,
        compute_pipelines_.data()
      );
      context_.releaseShaderModules(shaders);
    }

    // ------------------------------------

    /* OneSweep Radix Sort structures. */
    {
      radix_.histograms_sbo = context_.createBuffer(
        shader_interop::kRadixHistogramSize * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        // , VMA_MEMORY_USAGE_GPU_TO_CPU         // DEBUG
      );

      radix_.descriptor_size = vk_utils::GetKernelGridDim(
        splat_kv_heuristic_size_,
        shader_interop::kRadixSize
      ) * 3 * shader_interop::kRadixSize
      ;

      // (use an additional uint32_t for atomic counter)
      uint32_t const bufferSize = 1u + radix_.descriptor_size;

      radix_.prefix_descriptor_sbo = context_.createBuffer(
        bufferSize * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      );

      radix_.layout = context_.createPipelineLayout({
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(radix_.push_constant),
          }
        },
      });
      auto shaders = context_.createShaderModules(SAMPLE_SPIRV_DIR, {
        "radix_histogram.slang",
        "radix_prefix_sum.slang",
        "radix_binning.slang",
      });
      context_.createComputePipelines(
        radix_.layout,
        shaders,
        radix_.pipelines.data()
      );
      context_.releaseShaderModules(shaders);
    }

    return true;
  }

  void buildUI() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Separator();
    }
    ImGui::End();
  }

  void release() final {
    /* Gaussian Splatting */
    for (auto pipeline : compute_pipelines_) {
      context_.destroyPipeline(pipeline);
    }
    context_.destroyResources(
      pipeline_layout_,
      uniform_buffer_,
      gaussian_sbo_,
      splat_sbo_,
      splat_tilecount_sbo_,
      splat_keys_sbo_,
      splat_values_sbo_,
      prefix_output_sbo_,
      prefix_descriptor_sbo_,
      indirect_kv_count_sbo_
    );

    /* Radix */
    for (auto pipeline : radix_.pipelines) {
      context_.destroyPipeline(pipeline);
    }
    context_.destroyResources(
      radix_.layout,
      radix_.histograms_sbo,
      radix_.prefix_descriptor_sbo
    );
  }

  /* Compute Splats tiles offsets. */
  void dispatchPrefixSum(
    uint32_t const inputSize,
    backend::Buffer const& input,
    backend::Buffer const& output,
    backend::Buffer const& descriptor,
    backend::Buffer const& total_indirect
  ) {
    if (inputSize == 0) {
      return;
    }

    // ---------------
    uint32_t const groupCount = vk_utils::GetKernelGridDim(
      inputSize,
      shader_interop::kCompute_PrefixSum_kernelSize_x
    );

    auto const tileCounterOffset = VkDeviceSize{
      3u * groupCount * sizeof(uint32_t)
    };
    // ---------------

    push_constant_.numElems   = inputSize;
    push_constant_.tileSize   = shader_interop::kTileSize;
    push_constant_.radixSize  = shader_interop::kRadixSize;

    push_constant_.scan_input_addr      = input.address;
    push_constant_.scan_output_addr     = output.address;
    push_constant_.scan_descriptor_addr = descriptor.address;
    push_constant_.scan_counter_addr    = descriptor.address + tileCounterOffset;
    push_constant_.scan_total_count_indirect_addr = total_indirect.address;

    // ---------------

    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);

    cmd.pushConstant(push_constant_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

    // 1. Clear both descriptor flags and atomic counter.
    {
      cmd.fillBuffer(descriptor, 0u);

      // (debug only, should not be needed)
      cmd.fillBuffer(output, 0u);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .buffer = input.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_SHADER_WRITE_BIT
                         ,
          .buffer = output.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_SHADER_WRITE_BIT
                         ,
          .buffer = descriptor.buffer,
        },
      });
    }

    // 2. Run the PrefixSum
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_DecoupledPrefixSum]);
      cmd.dispatch(groupCount);
    }

    // 3. Calculate the total count and reset the indirect dispatch buffers.
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_ResetIndirectBuffers]);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .buffer = output.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_NONE, //
          .srcAccessMask = VK_ACCESS_NONE, //
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .buffer = total_indirect.buffer,
        },
      });

      cmd.dispatch();

      // [DEBUG]
      // cmd.pipelineBufferBarriers({
      //   {
      //     .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
      //     .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
      //     .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
      //     .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
      //     .buffer = total_indirect.buffer,
      //   },
      // });
    }

    context_.finishTransientCommandEncoder(cmd);

    // LOGI("> mapping post prefix Indirect + Total Count");
    // debugMapBuffer(total_indirect, 0, 4, 4);
  }

  void dispatchRadixSort(
    backend::Buffer const& indirect_key_count,
    backend::Buffer const& keys,
    backend::Buffer const& values
  ) {
    auto const& histograms = radix_.histograms_sbo;
    auto const& descriptor = radix_.prefix_descriptor_sbo;

    auto const keys_buffer_size   = VkDeviceAddress(splat_kv_heuristic_size_ * sizeof(uint64_t));
    auto const values_buffer_size = VkDeviceAddress(splat_kv_heuristic_size_ * sizeof(uint32_t));

    auto &pc = radix_.push_constant;
    pc.numkeys_addr         = indirect_key_count.address + key_count_offset_;
    pc.histogram_addr       = histograms.address;
    pc.unsorted_keys_addr   = keys.address;
    pc.unsorted_values_addr = values.address;
    pc.sorted_keys_addr     = keys.address + keys_buffer_size;
    pc.sorted_values_addr   = values.address + values_buffer_size;
    pc.descriptor_addr      = descriptor.address;
    pc.counter_addr         = descriptor.address + radix_.descriptor_size * sizeof(uint32_t);
    pc.pass                 = {};

    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);

    cmd.pushConstant(pc, radix_.layout, VK_SHADER_STAGE_COMPUTE_BIT);

    // 1. Compute Histograms
    {
      cmd.fillBuffer(histograms, 0u);
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
                        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                        ,
          .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                         | VK_ACCESS_2_SHADER_READ_BIT
                         ,
          .buffer = indirect_key_count.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = keys.buffer,
          .size   = static_cast<VkDeviceSize>(keys_buffer_size),
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer = histograms.buffer,
        }
      });

      cmd.bindPipeline(radix_.pipelines[RadixCompute_Histogram]);

      cmd.dispatchIndirect(indirect_key_count, indirect_histogram_offset_);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_SHADER_WRITE_BIT
                         ,
          .buffer = histograms.buffer,
        },
      });
    }

    // 2. Exclusive Sum (Global Prefix Scan on Histograms)
    {
      cmd.bindPipeline(radix_.pipelines[RadixCompute_PrefixSum]);

      cmd.dispatch(shader_interop::kRadixNumPasses);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .buffer = histograms.buffer,
        },
      });
    }

    // 3. OneSweep Digit Binning Kernel
    cmd.bindPipeline(radix_.pipelines[RadixCompute_Binning]);

    auto src_keys = keys.address;
    auto dst_keys = keys.address + keys_buffer_size;
    auto src_vals = values.address;
    auto dst_vals = values.address + values_buffer_size;

    for (uint32_t pass = 0; pass < shader_interop::kRadixNumPasses; ++pass)
    {
      if (pass > 0) {
        cmd.pipelineBufferBarriers({
          {
            .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                           | VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .buffer        = descriptor.buffer,
          }
        });
      }
      // Clear descriptors AND atomic tile counter
      cmd.fillBuffer(descriptor, 0u);
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_SHADER_WRITE_BIT,
          .buffer = descriptor.buffer,
        }
      });

      pc.pass                   = pass;
      pc.unsorted_keys_addr     = src_keys;
      pc.unsorted_values_addr   = src_vals;
      pc.sorted_keys_addr       = dst_keys;
      pc.sorted_values_addr     = dst_vals;
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .buffer        = keys.buffer,
          .offset        = src_keys - keys.address,
          .size          = keys_buffer_size,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .buffer        = keys.buffer,
          .offset        = dst_keys - keys.address,
          .size          = keys_buffer_size,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .buffer        = values.buffer,
          .offset        = src_vals - values.address,
          .size          = values_buffer_size,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .buffer        = values.buffer,
          .offset        = dst_vals - values.address,
          .size          = values_buffer_size,
        },
      });

      cmd.dispatchIndirect(indirect_key_count, indirect_binning_offset_);

      std::swap(src_keys, dst_keys);
      std::swap(src_vals, dst_vals);
    }

    // [DEBUG]
    if constexpr (kEnableDebugRun)
    cmd.pipelineBufferBarriers({
      {
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .buffer = keys.buffer,
      },
      {
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .buffer = histograms.buffer,
      },
    });

    context_.finishTransientCommandEncoder(cmd);

    // [ As kRadixNumPasses is even, the final sort is always the first section ]

    if constexpr (kEnableDebugRun)
    {
      LOGI("> map key outputs <first>");
      debugMapBuffer<uint64_t>(keys, 0u, kDebugBufferSize, 256u);

      LOGI("> map key outputs <second>");
      debugMapBuffer<uint64_t>(keys, kDebugBufferSize, 2*kDebugBufferSize, 256u);

      exit(-1);
    }
  }

  void update(float const dt) final {
    // Camera Uniform Data.
    host_data_.viewMatrix       = camera_.view();
    host_data_.projectionMatrix = camera_.proj();
    host_data_.tanFov           = camera_.tan_fovs();
    host_data_.focal            = camera_.focals();
    host_data_.resolution       = float2(
      static_cast<float>(camera_.width()),
      static_cast<float>(camera_.height())
    );
    context_.writeBuffer(uniform_buffer_, host_data_);

    // PushConstant buffers address.

    push_constant_.numElems               = gaussians_count_;
    push_constant_.maxCapacity            = gaussians_count_; //

    push_constant_.uniform_addr           = uniform_buffer_.address;
    push_constant_.gaussian_addr          = gaussian_sbo_.address;
    push_constant_.splat_addr             = splat_sbo_.address;
    push_constant_.scan_input_addr        = splat_tilecount_sbo_.address;

    push_constant_.unsorted_keys_addr     = splat_keys_sbo_.address;
    push_constant_.unsorted_values_addr   = splat_values_sbo_.address;

    // -------------------------------------------
    //  For debugging purpose we are currently using one command encoder
    //  per "pass", but later on we will use just one.
    // -------------------------------------------
#if 0
    // 1. Preprocess 3D Gaussian splats to tiled 2D screen space.
    {
      auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);

      push_constant_.numElems = gaussians_count_;

      cmd.bindPipeline(compute_pipelines_[GSCompute_Preprocess]);
      cmd.pushConstant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);

      // ------
      // BUGTRACK:
      //  There is a lot of tile per gaussians, which can be an issue with
      //   the kernel.
      // ------
      cmd.runKernel<shader_interop::kCompute_Preprocess_kernelSize_x>(
        push_constant_.numElems
      );

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
          .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
          .buffer = splat_tilecount_sbo_.buffer,
        },
      });

      context_.finishTransientCommandEncoder(cmd);

      // -------------------------------------------
      if constexpr (false && kEnableDebugRun)
      {
        LOGI("Total original Gaussian : {}", gaussians_count_);

        uint32_t nSize = push_constant_.numElems / 1500; //
        LOGI("> mapping TILE COUNT output {}/{} elements.", nSize, push_constant_.numElems);

        debugMapBuffer(splat_tilecount_sbo_, 0, nSize, 256);
      }
      // -------------------------------------------
    }

    // 2. Calculate tile offsets.
    dispatchPrefixSum(
      gaussians_count_,
      splat_tilecount_sbo_,
      prefix_output_sbo_,
      prefix_descriptor_sbo_,
      indirect_kv_count_sbo_
    );

    // 3. Create the keys-value pairs.
    {
      auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);

      push_constant_.numElems    = gaussians_count_;
      push_constant_.maxCapacity = splat_kv_heuristic_size_;

      cmd.bindPipeline(compute_pipelines_[GSCompute_DuplicateKeys]);
      cmd.pushConstant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);
      cmd.runKernel<shader_interop::kCompute_Duplicate_kernelSize_x>(
        push_constant_.numElems
      );

      context_.finishTransientCommandEncoder(cmd);
    }
#endif

    // 4. Sort key-value pairs.
    dispatchRadixSort(
      indirect_kv_count_sbo_,
      splat_keys_sbo_,
      splat_values_sbo_
    );


    if constexpr (kEnableDebugRun) {
      static int frame = 0;
      if (++frame == 3) {
        // exit(-1);
      }
      LOGI("------------------------------->");
    }
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    cmd.endRendering();
    drawUI(cmd);
  }

  template<typename T = uint32_t>
  void debugMapBuffer(backend::Buffer buf, uint32_t first, uint32_t last, uint32_t kernelSize)
  {
    T *outputs = nullptr;
    context_.mapMemory(buf, &outputs);

    for (uint32_t i = first; i < last; ++i) {
      if (i > 0 && (0 == i % kernelSize)) {
        fprintf(stderr, "| \n");
      }
      LOGD("({}) {}", i, outputs[i]);
    }
    fprintf(stderr, "\n");

    context_.unmapMemory(buf);
  }

 private:
  ArcBallController arcball_controller_{};

  shader_interop::UniformBufferData host_data_{};
  backend::Buffer uniform_buffer_{};

  // ----------

  backend::Buffer gaussian_sbo_{};          // raw input data

  backend::Buffer splat_sbo_{};             // preprocess output
  backend::Buffer splat_tilecount_sbo_{};   // overlapped tile count

  backend::Buffer splat_keys_sbo_{};        // double buffer of 64bits
  backend::Buffer splat_values_sbo_{};      // double buffer of 32bits
  VkDeviceSize splat_kv_heuristic_size_{};  //

  backend::Buffer prefix_output_sbo_{};
  backend::Buffer prefix_descriptor_sbo_{};

  backend::Buffer indirect_kv_count_sbo_{};
  VkDeviceSize key_count_offset_{};
  VkDeviceSize indirect_histogram_offset_{};
  VkDeviceSize indirect_binning_offset_{};

  VkPipelineLayout pipeline_layout_{};
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, GSCompute_kCount> compute_pipelines_{};
  uint32_t gaussians_count_{};

  // ----------

  struct Radix {
    backend::Buffer histograms_sbo{};
    backend::Buffer prefix_descriptor_sbo{};
    VkDeviceSize descriptor_size{};

    VkPipelineLayout layout{};
    shader_interop::RadixPushConstant push_constant{};
    std::array<Pipeline, RadixCompute_kCount> pipelines{};
  } radix_;
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
