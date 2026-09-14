/* -------------------------------------------------------------------------- */
//
//    13 - Gaussian Splatting
//
//  Where we go beyond the triangle.
//
/* -------------------------------------------------------------------------- */

#include <random>

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "miniply.h"

namespace shader_interop {
#include "shaders/interop.h"
#include "shaders/radix_interop.h"
}

// ----------------------------------------------------------------------------

class GaussianSplatSample final : public Application {
 public:
  static constexpr uint32_t kHeuristicMaxTilePerGaussian{ 8 }; //

  static constexpr bool kEnableDebugRun{ false };
  static constexpr uint32_t kDebugBufferSize{ 1 << 8 };

  public:
    enum QueryTimestamp {
      QueryTimestamp_Start,
      QueryTimestamp_End,

      QueryTimestamp_kCount,
    };

    enum GSCompute {
      GSCompute_Preprocess,
      GSCompute_DecoupledPrefixSum,
      GSCompute_ResetIndirectBuffers,
      GSCompute_DuplicateKeys,
      GSCompute_IdentifyTileRanges,
      GSCompute_Rasterize,

      GSCompute_kCount,
    };

    enum RadixCompute {
      RadixCompute_Histogram,
      RadixCompute_PrefixSum,
      RadixCompute_ClearDescriptor,
      RadixCompute_Binning,

      RadixCompute_kCount
    };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.sample_count = VK_SAMPLE_COUNT_1_BIT;
    S.app_name = "Gaussian Splat Viewer";
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
      arcball_controller_.set_dolly(5.0f);
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

    query_pool_ = context_.createQueryPool(
      VK_QUERY_TYPE_TIMESTAMP, QueryTimestamp_kCount
    );

    /* Import point cloud (ply) data */
    std::vector<shader_interop::GaussianData> gaussians{};
    {
      auto reader = miniply::PLYReader( ASSETS_DIR "pointclouds/"
        "flowers_1/flowers_1.ply"
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
      gaussians.resize(gaussians_count_, shader_interop::GaussianData{});

      constexpr uint32_t kPropCount = 59u;
      std::array<uint32_t, kPropCount> indexes{};

      bool res = reader.find_properties(indexes.data(), kPropCount,
        "x", "y", "z",
        "rot_1", "rot_2", "rot_3", "rot_0", // 'w' at the end
        "scale_0", "scale_1", "scale_2", "opacity",
        "f_dc_0",
        "f_dc_1",
        "f_dc_2",
        "f_rest_0",
        "f_rest_1",
        "f_rest_2",
        "f_rest_3",
        "f_rest_4",
        "f_rest_5",
        "f_rest_6",
        "f_rest_7",
        "f_rest_8",
        "f_rest_9",
        "f_rest_10",
        "f_rest_11",
        "f_rest_12",
        "f_rest_13",
        "f_rest_14",
        "f_rest_15",
        "f_rest_16",
        "f_rest_17",
        "f_rest_18",
        "f_rest_19",
        "f_rest_20",
        "f_rest_21",
        "f_rest_22",
        "f_rest_23",
        "f_rest_24",
        "f_rest_25",
        "f_rest_26",
        "f_rest_27",
        "f_rest_28",
        "f_rest_29",
        "f_rest_30",
        "f_rest_31",
        "f_rest_32",
        "f_rest_33",
        "f_rest_34",
        "f_rest_35",
        "f_rest_36",
        "f_rest_37",
        "f_rest_38",
        "f_rest_39",
        "f_rest_40",
        "f_rest_41",
        "f_rest_42",
        "f_rest_43",
        "f_rest_44"
      );
      if (!res) {
        LOGW("miniply: one or more properties not found in the PLY header!");
        return false;
      }

      auto const stride = sizeof(shader_interop::GaussianData);

      // Extract properties individually to count for paddings.
      reader.extract_properties_with_stride(
        &indexes[0], 3, miniply::PLYPropertyType::Float,
        &gaussians[0].position, stride
      );
      reader.extract_properties_with_stride(
        &indexes[3], 4, miniply::PLYPropertyType::Float,
        &gaussians[0].rotation, stride
      );
      reader.extract_properties_with_stride(
        &indexes[7], 4, miniply::PLYPropertyType::Float,
        &gaussians[0].scale_opacity, stride
      );
      reader.extract_properties_with_stride(
        &indexes[11], 48, miniply::PLYPropertyType::Float,
        gaussians[0].shc, stride
      );
    }

    // [ later on, would be better to allocate large buffer we subaddress ]

    if constexpr(kEnableDebugRun)
    {
      LOGW(">>>> DEBUG_RUN is ON <<<<");
      gaussians_count_ = kDebugBufferSize; //
    }

    constexpr VmaMemoryUsage kDefaultBufferMemoryUsage{
      kEnableDebugRun ? VMA_MEMORY_USAGE_GPU_TO_CPU
                      : VMA_MEMORY_USAGE_GPU_ONLY
    };

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
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        kDefaultBufferMemoryUsage
      );

      // As we don't know how many total tiles would be touched
      // we must allocated a maximum buffer based off an heuristic factor.
      splat_kv_heuristic_size_ = gaussians_count_
                               * kHeuristicMaxTilePerGaussian;

      splat_keys_sbo_ = context_.createBuffer(
        2u * splat_kv_heuristic_size_ * sizeof(uint64_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        kDefaultBufferMemoryUsage
      );
      if constexpr (kEnableDebugRun)
      {
        uint64_t *buffer;
        context_.mapMemory(splat_keys_sbo_, &buffer);
          std::random_device rd;
          std::mt19937_64 gen(rd());
          std::uniform_int_distribution<uint64_t> distrib(0, UINT64_MAX);
          for (size_t i = 0; i < splat_kv_heuristic_size_; ++i) {
            buffer[i] = distrib(gen);
          }
        context_.unmapMemory(splat_keys_sbo_);
      }

      splat_values_sbo_ = context_.createBuffer(
        2u * splat_kv_heuristic_size_ * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        kDefaultBufferMemoryUsage
      );
    }

    /* PrefixSum buffers. */
    {
      if constexpr(kEnableDebugRun)
      {
        std::vector<uint32_t> counts(gaussians_count_, 0);
        for (size_t i = 0; (i<kDebugBufferSize) && (i<counts.size()); ++i) {
          // BUGTRACK: force a possible oveflow problem
          counts[i] = 2 + rand() % kHeuristicMaxTilePerGaussian; //<
        }

        splat_tilecount_sbo_ = context_.transientCreateBuffer(
          counts,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          kDefaultBufferMemoryUsage
        );
      }
      else
      {
        splat_tilecount_sbo_ = context_.createBuffer(
          gaussians_count_ * sizeof(uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
          | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          kDefaultBufferMemoryUsage
        );
      }

      prefix_output_sbo_ = context_.createBuffer(
        gaussians_count_ * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          kDefaultBufferMemoryUsage
      );

      // --------------------------------------

      // This prefix is used to calculate tile count *per splats*.
      uint32_t const prefixDescriptorBufferSize = 3u * vk_utils::GetKernelGridDim(
        gaussians_count_,
        shader_interop::kCompute_PrefixSum_kernelSize_x
      );

      // Hold 1 atomic counter + descriptor flags.
      prefix_descriptor_and_count_sbo_ = context_.createBuffer(
        (1u + prefixDescriptorBufferSize) * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          kDefaultBufferMemoryUsage
      );
      prefix_descriptor_count_offset_ = prefixDescriptorBufferSize
                                      * sizeof(uint32_t);

      // - The 3 first uint32_t are X,Y,Z gridDim for the radix histogram.
      // - the 4th uint32_t is the total size of keys (prefixSum total).
      // - the 5th, 6th, 7th are X,Y,Z gridDim for the radix digit binning.
      indirect_kv_count_sbo_ = context_.createBuffer(
        8u * sizeof(uint32_t), //
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
          kDefaultBufferMemoryUsage
      );
      key_count_offset_           = 3u * sizeof(uint32_t);
      indirect_histogram_offset_  = 0u;
      indirect_binning_offset_    = 4u * sizeof(uint32_t);

      // if we bypass the gaussian pipeline / prefix sum
      // we need to specify the (debug) indirect buffer directly.
      // THIS USE AN HEURISTIC THO !!!
      if constexpr (kEnableDebugRun) {
        uint32_t *buffer;
        context_.mapMemory(indirect_kv_count_sbo_, &buffer);
          buffer[0] = vk_utils::GetKernelGridDim(
            splat_kv_heuristic_size_,
            shader_interop::kTileSize
          );
          buffer[1] = 1;
          buffer[2] = 1;

          buffer[3] = splat_kv_heuristic_size_; //

          buffer[4] = vk_utils::GetKernelGridDim(
            splat_kv_heuristic_size_,
            shader_interop::kRadixSize
          );
          buffer[5] = 1;
          buffer[6] = 1;
        context_.unmapMemory(indirect_kv_count_sbo_);
      }

      // LOGI("> res {} {}", viewport_size_.width, viewport_size_.height);

      tile_count_x_ = vk_utils::GetKernelGridDim(viewport_size_.width, shader_interop::kTileResolution);
      tile_count_y_ = vk_utils::GetKernelGridDim(viewport_size_.height, shader_interop::kTileResolution);

      tile_ranges_sbo_ = context_.createBuffer(
        tile_count_x_ * tile_count_y_ * 2u * sizeof(uint32_t), //
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        ,
        kDefaultBufferMemoryUsage
      );
    }

    /* GS rasterizer output image */
    gs_image_ = context_.createImage2D(
      viewport_size_.width,
      viewport_size_.height,
      VK_FORMAT_R16G16B16A16_SFLOAT, // VK_FORMAT_B8G8R8A8_UNORM,
        VK_IMAGE_USAGE_STORAGE_BIT
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
      | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
      // | VK_IMAGE_USAGE_SAMPLED_BIT
      ,
      "GaussianSplatting::OutputImage"
    );

    /* Descriptor set. */
    {
      descriptor_set_layout_ = context_.createDescriptorSetLayout({
        {
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        }
      });
      descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
        {
          .binding = 0,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .images = {
            {
              .imageView = gs_image_.view,
              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
            }
          }
        },
      });
    }

    /* Create the Compute Pipelines */
    {
      pipeline_layout_ = context_.createPipelineLayout({
        .setLayouts = { descriptor_set_layout_ },
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
        "gs_identify_tile_ranges.slang",
        "gs_rasterize.slang"
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
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          kDefaultBufferMemoryUsage
      );

      radix_.descriptor_size = vk_utils::GetKernelGridDim(
        splat_kv_heuristic_size_,
        shader_interop::kRadixSize
      ) * 3 * shader_interop::kRadixSize
      ;

      // (use an additional uint32_t for atomic counter)
      uint32_t const singlePassSize = 1u + radix_.descriptor_size;

      // [should we N-buffer it ?]
      // uint32_t const totalBufferSize = singlePassSize * shader_interop::kRadixNumPasses;
      // LOGI("single buffer size {} Mb", singlePassSize / (1024*1024));
      // LOGI("total buffer size {} Mb", totalBufferSize / (1024*1024));

      radix_.prefix_descriptor_sbo = context_.createBuffer(
        singlePassSize * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
          kDefaultBufferMemoryUsage
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
        "radix_clear_descriptor.slang",
        "radix_binning.slang",
      });
      context_.createComputePipelines(
        radix_.layout,
        shaders,
        radix_.pipelines.data()
      );
      context_.releaseShaderModules(shaders);
    }

    // PushConstant base setup.
    {
      auto &pc = push_constant_;

      pc.numElems               = gaussians_count_;
      pc.maxKeyValueCapacity    = splat_kv_heuristic_size_;

      pc.tileSize               = shader_interop::kTileSize;
      pc.radixSize              = shader_interop::kRadixSize;

      pc.uniform_addr           = uniform_buffer_.address;
      pc.gaussian_addr          = gaussian_sbo_.address;
      pc.splat_addr             = splat_sbo_.address;

      pc.scan_input_addr        = splat_tilecount_sbo_.address;
      pc.scan_output_addr       = prefix_output_sbo_.address;
      pc.scan_descriptor_addr   = prefix_descriptor_and_count_sbo_.address;
      pc.scan_counter_addr      = prefix_descriptor_and_count_sbo_.address
                                + prefix_descriptor_count_offset_;

      pc.indirect_count_addr    = indirect_kv_count_sbo_.address;
      pc.numkeys_addr           = indirect_kv_count_sbo_.address
                                + key_count_offset_;

      pc.keys_addr              = splat_keys_sbo_.address;
      pc.values_addr            = splat_values_sbo_.address;
      pc.tile_ranges_addr       = tile_ranges_sbo_.address;
    }

    LOGD("TileSize is {}", shader_interop::kTileSize);
    LOGD("RadixSize is {}", shader_interop::kRadixSize);

    // Setup initial uniform buffer.
    {
      host_data_.viewMatrix       = camera_.view();
      host_data_.projectionMatrix = camera_.proj();
      host_data_.tanFov           = camera_.tan_fovs();
      host_data_.focal            = camera_.focals();
      host_data_.resolution       = float2(
        static_cast<float>(camera_.width()),
        static_cast<float>(camera_.height())
      );
      context_.writeBuffer(uniform_buffer_, host_data_);
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
      query_pool_,
      pipeline_layout_,
      uniform_buffer_,
      gaussian_sbo_,
      splat_sbo_,
      splat_tilecount_sbo_,
      splat_keys_sbo_,
      splat_values_sbo_,
      prefix_output_sbo_,
      prefix_descriptor_and_count_sbo_,
      indirect_kv_count_sbo_,
      tile_ranges_sbo_,
      gs_image_,
      descriptor_set_layout_
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
    CommandEncoder const& cmd,
    uint32_t const inputSize,
    backend::Buffer const& input,
    backend::Buffer const& output,
    backend::Buffer const& descriptor,
    backend::Buffer const& total_indirect
  ) {
    LOG_CHECK(inputSize != 0);

    auto pc = push_constant_;
    pc.numElems             = inputSize;
    pc.maxKeyValueCapacity  = splat_kv_heuristic_size_;
    pc.scan_input_addr      = input.address;
    pc.scan_output_addr     = output.address;
    pc.scan_descriptor_addr = descriptor.address;
    pc.scan_counter_addr    = descriptor.address + prefix_descriptor_count_offset_;
    pc.indirect_count_addr = total_indirect.address;
    cmd.pushConstant(pc, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

    // 1. Clear both descriptor flags AND the atomic counter.
    {
      cmd.fillBuffer(descriptor, 0u);
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = input.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                         ,
          .buffer = output.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                         ,
          .buffer = descriptor.buffer,
        },
      });
    }

    // 2. Run the PrefixSum
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_DecoupledPrefixSum]);
      cmd.runKernel<shader_interop::kCompute_PrefixSum_kernelSize_x>(
        pc.numElems
      );
    }

    // 3. Calculate the total count and reset the indirect dispatch buffers.
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_ResetIndirectBuffers]);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = output.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_NONE, //
          .srcAccessMask = VK_ACCESS_NONE, //
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer = total_indirect.buffer,
        },
      });

      cmd.dispatch();
    }
  }

  void dispatchRadixSort(
    CommandEncoder const& cmd,
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
    cmd.pushConstant(pc, radix_.layout, VK_SHADER_STAGE_COMPUTE_BIT);

    // 1. Compute Histogram.
    {
      // Reset [todo in shader instead ?]
      cmd.fillBuffer(histograms, 0u);
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT
                        | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                        ,
          .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         ,
          .buffer = indirect_key_count.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
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
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
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
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
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

    // [ As kRadixNumPasses is even, the final sort is always the first section ]
    for (uint32_t pass = 0; pass < shader_interop::kRadixNumPasses; ++pass)
    {
      pc.pass                   = pass;
      pc.unsorted_keys_addr     = src_keys;
      pc.unsorted_values_addr   = src_vals;
      pc.sorted_keys_addr       = dst_keys;
      pc.sorted_values_addr     = dst_vals;
      cmd.pushConstant(pc, radix_.layout, VK_SHADER_STAGE_COMPUTE_BIT);

      // Clear descriptors AND atomic tile counter.
      // -------------------------------------
      // [probably have room for optimizations here]
#if 0
      if (pass > 0) {
        cmd.pipelineBufferBarriers({
          {
            .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                           | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .buffer        = descriptor.buffer,
          }
        });
      }
      cmd.fillBuffer(descriptor, 0u);
#else
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = descriptor.buffer,
        }
      });
      cmd.bindPipeline(radix_.pipelines[RadixCompute_ClearDescriptor]);
      cmd.dispatchIndirect(indirect_key_count, indirect_binning_offset_);

      cmd.bindPipeline(radix_.pipelines[RadixCompute_Binning]);
#endif
      // -------------------------------------

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                         | VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                         | VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = descriptor.buffer,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = keys.buffer,
          .offset        = src_keys - keys.address,
          .size          = keys_buffer_size,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = keys.buffer,
          .offset        = dst_keys - keys.address,
          .size          = keys_buffer_size,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = values.buffer,
          .offset        = src_vals - values.address,
          .size          = values_buffer_size,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = values.buffer,
          .offset        = dst_vals - values.address,
          .size          = values_buffer_size,
        },
      });
      cmd.dispatchIndirect(indirect_key_count, indirect_binning_offset_);

      std::swap(src_keys, dst_keys);
      std::swap(src_vals, dst_vals);
    }
  }

  void runGaussianSplattingPipeline(CommandEncoder const& cmd) {
    // Shared push constants.
    auto pc = push_constant_;
    cmd.pushConstant(pc, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

    // 0. Reset output images & buffers.
    {
      if (frame_index() == 0) {
        cmd.transitionColorImages(
          { gs_image_ },
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_GENERAL
        );
      }

      // (debug GS rasterizer clear color)
      auto tile_clear_color = vec4(0.8f, 0.5f, 0.8f, 1.0f);
      cmd.clearColorImage(gs_image_, tile_clear_color);

      cmd.fillBuffer(tile_ranges_sbo_, 0u); //
    }

    // 1. Preprocess 3D Gaussian splats to tiled 2D screen space.
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_Preprocess]);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = prefix_output_sbo_.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = splat_tilecount_sbo_.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer = splat_keys_sbo_.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer = splat_values_sbo_.buffer,
        },
      });

      cmd.runKernel<shader_interop::kCompute_Preprocess_kernelSize_x>(
        push_constant_.numElems
      );
    }

    // 2. Calculate tile offsets.
    dispatchPrefixSum(
      cmd,
      gaussians_count_,
      splat_tilecount_sbo_,
      prefix_output_sbo_,
      prefix_descriptor_and_count_sbo_,
      indirect_kv_count_sbo_
    );

    // 3. Create the keys-value pairs.
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_DuplicateKeys]);

      auto kv_pc = push_constant_;
      kv_pc.numElems            = gaussians_count_;
      kv_pc.maxKeyValueCapacity = splat_kv_heuristic_size_;
      cmd.pushConstant(kv_pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = splat_tilecount_sbo_.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer = prefix_output_sbo_.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer = splat_keys_sbo_.buffer,
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer = splat_values_sbo_.buffer,
        },
      });

      // For each Gaussian's touched tiles, create a key-value pair.
      cmd.runKernel<shader_interop::kCompute_Duplicate_kernelSize_x>(
        push_constant_.numElems
      );
    }

    // 4. Sort key-value pairs.
    // [This use 64bits key as per the original 2023 GS paper, but we might be
    //  able to halve it to 32bits for performance.]
    dispatchRadixSort(
      cmd,
      indirect_kv_count_sbo_,
      splat_keys_sbo_,
      splat_values_sbo_
    );

    // [important]
    // we must update the push constant after the RadixSort, which uses its own.
    cmd.pushConstant(pc, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

    // 5. Identify Tile Ranges.
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_IdentifyTileRanges]);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = splat_keys_sbo_.buffer,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = tile_ranges_sbo_.buffer,
        },
      });

      // (Use the same kernel size than the radix binning)
      cmd.dispatchIndirect(indirect_kv_count_sbo_, indirect_binning_offset_);
    }

    // 6. Rasterize
    {
      cmd.bindPipeline(compute_pipelines_[GSCompute_Rasterize]);
      cmd.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = tile_ranges_sbo_.buffer,
        },
      });
      cmd.pipelineImageBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
          .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                         | VK_ACCESS_2_SHADER_WRITE_BIT
                         ,
          .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
          .newLayout = VK_IMAGE_LAYOUT_GENERAL,
          .image = gs_image_.image,
          .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } //
        },
      });

#if 1
      cmd.dispatch(tile_count_x_, tile_count_y_); //
#else
      cmd.runKernel<shader_interop::kTileResolution, shader_interop::kTileResolution>(
        viewport_size_.width,
        viewport_size_.height
      );
#endif
    }
  }

  void update(float const dt) final {
    // Camera Uniform Data.
    {
      host_data_.viewMatrix = camera_.view();
      context_.writeBuffer(uniform_buffer_, host_data_); //
    }

    // ---------------------------------------------

#if 0
    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);
    {
      cmd.resetQueryPool(query_pool_, QueryTimestamp_Start, QueryTimestamp_kCount);
      cmd.writeTimestamp(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, query_pool_, QueryTimestamp_Start);

      runGaussianSplattingPipeline(cmd);

      cmd.writeTimestamp(VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, query_pool_, QueryTimestamp_End);
    }
    context_.finishTransientCommandEncoder(cmd);
#endif

    // ---------------------------------------------

    // Query previous frame Timestamps.
    if (frame_index() > 0)
    {
      // To avoid CPU bottlenecks when using VK_QUERY_RESULT_WAIT_BIT we fetch
      // previous frame's result once its synchronization fence signals.
      if constexpr (false)
      {
        std::array<uint64_t, QueryTimestamp_kCount> timestamps{};

        auto printElapsedTime = [&C = this->context_, &timestamps](std::string const& name, uint32_t start, uint32_t end) {
          uint64_t const elapsedTicks = timestamps[end] - timestamps[start];
          double const elapsedPeriod = C.gpu_properties().limits.timestampPeriod;
          double const elapsedMillis = static_cast<double>(
            (elapsedTicks * elapsedPeriod) / 1e6
          );
          LOGI("> {} : {:0.2f} ms.", name, elapsedMillis);
        };

        auto res = context_.getQueryPoolResults(
          query_pool_,
          QueryTimestamp_Start,
          QueryTimestamp_kCount,
          sizeof(timestamps),
          timestamps.data(),
          sizeof(timestamps[0]),
          VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if (VK_SUCCESS == res) {
          printElapsedTime("runGaussianSplattingPipeline", QueryTimestamp_Start, QueryTimestamp_End);
        }
      }

      if constexpr (kEnableDebugRun)
      {
        LOGI("> post sort KEYS output <first>");
        debugMapBuffer<uint64_t>(splat_keys_sbo_, 0u, kDebugBufferSize, 256u);
        exit(-1);
      }
    }
  }

  void draw(CommandEncoder const& cmd) final {
    runGaussianSplattingPipeline(cmd);

    // auto pass = cmd.beginRendering();
    // cmd.endRendering();

    // Test display result
    cmd.transitionColorImages({gs_image_}, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    renderer_.blitColor(cmd, gs_image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    cmd.transitionColorImages({gs_image_}, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    drawUI(cmd);

    if constexpr (kEnableDebugRun)
    cmd.pipelineBufferBarriers({
      {
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
        .buffer = splat_keys_sbo_.buffer,
      },
    });
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
      LOGD("{:04d}. {}", i, outputs[i]);
    }
    fprintf(stderr, "\n");

    context_.unmapMemory(buf);
  }

 private:
  ArcBallController arcball_controller_{};

  shader_interop::UniformBufferData host_data_{};
  backend::Buffer uniform_buffer_{};

  VkQueryPool query_pool_{};

  // ----------

  backend::Buffer gaussian_sbo_{};          // raw input data

  backend::Buffer splat_sbo_{};             // preprocess output
  backend::Buffer splat_tilecount_sbo_{};   // overlapped tile count

  backend::Buffer splat_keys_sbo_{};        // double buffer of 64bits
  backend::Buffer splat_values_sbo_{};      // double buffer of 32bits
  VkDeviceSize splat_kv_heuristic_size_{};  // max key-value pair accepted.

  backend::Buffer prefix_output_sbo_{};     // => tile offsets
  backend::Buffer prefix_descriptor_and_count_sbo_{}; // internal to exclusive prefix
  VkDeviceSize prefix_descriptor_count_offset_{};

  backend::Buffer indirect_kv_count_sbo_{}; // => radix indirects & total tiles
  VkDeviceSize key_count_offset_{};
  VkDeviceSize indirect_histogram_offset_{};
  VkDeviceSize indirect_binning_offset_{};

  uint32_t tile_count_x_{};
  uint32_t tile_count_y_{};
  backend::Buffer tile_ranges_sbo_{};

  backend::Image gs_image_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
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

ENTRY_POINT(GaussianSplatSample)

/* -------------------------------------------------------------------------- */
