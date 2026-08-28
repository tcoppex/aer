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
}

// ----------------------------------------------------------------------------

// peut avoir des bugs avec un kernel de 64 bits, probleme due à un prefix sum
// sur des waves inexistantes

uint32_t const kDebugCount = 83;

class SampleApp final : public Application {
  public:
  enum GSCompute {
    GSCompute_Preprocess  = 0,
    GSCompute_ScanUp,
    GSCompute_ScanDown,

    GSCompute_kCount,
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

      /* Allocate device buffers. */

      LOGI(">>> Start allocating Gaussian Splat buffers");
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
      LOGI("<<< End allocating Gaussian Splat buffers");
    }

    /* Allocate the PrefixSum buffers */
    {
      uint32_t const kPrefixWorkGroupSize = shader_interop::kCompute_PrefixSum_kernelSize_x;

      uint32_t kNumElems = vk_utils::GetPaddingCount(
        kDebugCount,
        // gaussians_count_,

        kPrefixWorkGroupSize
      );

      // Initialize the test buffer.
      std::vector<uint32_t> counts(kNumElems, 0);
      for (size_t i = 0; (i<kDebugCount) && (i<counts.size()); ++i) {
        counts[i] = 1;
      }

      //--------------
      // prefix_count_sbo_ = context_.createBuffer(
      //   kMaxPrefixSum * sizeof(uint32_t),
      //     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      //   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      // );
      prefix_count_sbo_ = context_.transientCreateBuffer(
        counts,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      );

      prefix_output_local_sbo_ = context_.createBuffer(
        kNumElems * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        , VMA_MEMORY_USAGE_GPU_TO_CPU
      );
      //--------------

      // PrefixSum
      // Each level of the up-sweep phase needs two buffers the size of
      // the level groupCount :
      //  - 1 for the previous pass block offset output
      //  - 1 for the current phase local offset output

      uint32_t scratchBufferSize = 0;
      uint32_t level = 0;
      for(uint32_t size = kNumElems; size > 1; level++) {
        size = vk_utils::GetKernelGridDim(size, kPrefixWorkGroupSize);
        scratchBufferSize += 2*size;

        LOGW("level {}, + {}  = {}", level, size,scratchBufferSize);
      }
      LOGW("prefix sum buffer size is {}, with {} levels", scratchBufferSize, level);

      prefix_scratch_sbo_ = context_.createBuffer(
        scratchBufferSize * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        , VMA_MEMORY_USAGE_GPU_TO_CPU
      );
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
        "gaussian_preprocess.slang",
        "prefix_sum.slang",
      });

      context_.createComputePipelines(
        pipeline_layout_,
        ShaderStageDescriptors{
          { shaders[0] },
          { shaders[1],   "localPrefixSum" },
          { shaders[1],   "addBlockOffsets" },
        },
        compute_pipelines_.data()
      );

      context_.releaseShaderModules(shaders);
    }

    // LOGI(
    //   "subgroupSize : {}",
    //   context_.subgroup_properties().subgroupSize
    // );

    return true;
  }

  void release() final {
    for (auto pipeline : compute_pipelines_) {
      context_.destroyPipeline(pipeline);
    }
    context_.destroyResources(
      pipeline_layout_,
      uniform_buffer_,
      gaussian_sbo_,
      splat_sbo_,

      prefix_count_sbo_,
      prefix_output_local_sbo_,
      prefix_scratch_sbo_
    );
  }

  void dispatchPrefixSum(
    uint32_t const inputSize,
    backend::Buffer const& input,
    backend::Buffer const& output,
    backend::Buffer const& scratch
  ) {
    using InternalType = uint32_t;

    uint32_t const kKernelSize = shader_interop::kCompute_PrefixSum_kernelSize_x;

    struct LevelInfo {
      uint32_t numElems{};
      uint32_t groupCount{};
      VkDeviceSize groupBufferSize{};
      VkDeviceAddress inputAddr{};
      VkDeviceAddress outputAddr{};
      VkDeviceAddress outputGroupAddr{};
    };
    std::vector<LevelInfo> levels{};

    /* --- Prefill levels params --- */

    uint32_t currentSize = inputSize;
    VkDeviceAddress currentInput  = input.address;
    VkDeviceAddress currentOutput = output.address;
    VkDeviceAddress groupOutput   = scratch.address;

    for (uint32_t levelIndex = 0; currentSize > 1; ++levelIndex)
    {
      auto groupCount = vk_utils::GetKernelGridDim(currentSize, kKernelSize);
      auto groupBufferSize = VkDeviceSize(groupCount * sizeof(InternalType));

      levels.emplace_back(LevelInfo{
        .numElems = currentSize,
        .groupCount = groupCount,
        .groupBufferSize = groupBufferSize,
        .inputAddr = currentInput,
        .outputAddr = currentOutput,
        .outputGroupAddr = groupOutput,
      });

      // Next level params (targets previous groupOutput).
      currentSize   = groupCount;
      currentInput  = groupOutput;
      currentOutput = groupOutput + groupBufferSize;     // current level '2nd side'
      groupOutput   = currentOutput + groupBufferSize;   // next level '1st side'
    }

    /* --- Up-Sweep --- */

    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);
    cmd.bindPipeline(compute_pipelines_[GSCompute_ScanUp]);

    for (auto const&l : levels)
    {
      push_constant_.numElems               = l.numElems;
      push_constant_.scan_input_addr        = l.inputAddr;
      push_constant_.scan_output_addr       = l.outputAddr;
      push_constant_.scan_output_group_addr = l.outputGroupAddr;
      cmd.pushConstant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch(l.groupCount);

      auto const nextReadOffset = static_cast<VkDeviceSize>(
        l.outputGroupAddr - scratch.address
      );
      cmd.pipelineBufferBarriers({
        // Previous Write, Next Read
        {
          .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
          .buffer = scratch.buffer,
          .offset = nextReadOffset,
          .size = l.groupBufferSize,
        },
      });
    }

    context_.finishTransientCommandEncoder(cmd);

    // for (auto const& l : levels | std::views::reverse) {
    //   // TODO
    // }

    // -------------------------------------------
    // uint32_t *outputs = nullptr;

    // LOGI("> mapping prefix local output");
    // context_.mapMemory(prefix_output_local_sbo_, &outputs);
    //   for (uint32_t i = 0; i < push_constant_.numElems; ++i) {
    //     if (i > 0 && (0 == i%shader_interop::kCompute_PrefixSum_kernelSize_x)) {
    //       fprintf(stderr, "| \n");
    //     }
    //     fprintf(stderr, "(%d) %d %s\n", i, outputs[i],
    //       (i%shader_interop::kCompute_PrefixSum_kernelSize_x != outputs[i])
    //         ? "X" : ""
    //     );
    //   }
    //   fprintf(stderr, "\n");
    // context_.unmapMemory(prefix_output_local_sbo_);

    // uint32_t const N = ceil(push_constant_.numElems / (float)shader_interop::kCompute_PrefixSum_kernelSize_x);
    // LOGI("> mapping prefix group output (N = {})", N);
    // context_.mapMemory(prefix_scratch_sbo_, &outputs);
    //   for (uint32_t i = 0; i < N; ++i) {
    //     fprintf(stderr, "%d ", outputs[i]);
    //   }
    //   fprintf(stderr, "\n");
    // context_.unmapMemory(prefix_scratch_sbo_);
    // -------------------------------------------
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
    push_constant_.uniform_addr     = uniform_buffer_.address;
    push_constant_.gaussian_addr    = gaussian_sbo_.address;
    push_constant_.splat_addr       = splat_sbo_.address;

    //--------------
    push_constant_.scan_input_addr        = prefix_count_sbo_.address;
    push_constant_.scan_output_addr = prefix_output_local_sbo_.address;
    push_constant_.scan_output_group_addr = prefix_scratch_sbo_.address;
    //--------------

    // -------------------------------------------

#if 0
    //WIP
    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Main);
    {
      // Preprocess
      {
        push_constant_.numElems = gaussians_count_;
        cmd.pushConstant(
          push_constant_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT
        );

        cmd.bindPipeline(compute_pipelines_[GSCompute_Preprocess]);

        cmd.runKernel<shader_interop::kCompute_Preprocess_kernelSize_x>(push_constant_.numElems);

        cmd.pipelineBufferBarriers({
          {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                           | VK_ACCESS_2_SHADER_WRITE_BIT,
            .buffer = splat_sbo_.buffer,
          }
        });
      }
    }
    context_.finishTransientCommandEncoder(cmd);
#endif

    dispatchPrefixSum(
      kDebugCount,
      prefix_count_sbo_,
      prefix_output_local_sbo_,
      prefix_scratch_sbo_
    );

    // exit(-1);
  }

  void draw(CommandEncoder const& cmd) final {

    auto pass = cmd.beginRendering();
    {
    }
    cmd.endRendering();

    drawUI(cmd);
  }

  void buildUI() final {}

 private:
  ArcBallController arcball_controller_{};

  shader_interop::UniformBufferData host_data_{};
  backend::Buffer uniform_buffer_{};

  backend::Buffer gaussian_sbo_{};
  backend::Buffer splat_sbo_{};

  backend::Buffer prefix_count_sbo_{}; //
  backend::Buffer prefix_output_local_sbo_{}; //
  backend::Buffer prefix_scratch_sbo_{}; //

  VkPipelineLayout pipeline_layout_{};
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, GSCompute_kCount> compute_pipelines_{};
  uint32_t gaussians_count_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
