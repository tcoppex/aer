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

uint32_t const kDebugCount = 278;

class SampleApp final : public Application {
  public:
  enum GSCompute {
    GSCompute_Preprocess  = 0,
    GSCompute_PrefixSum,

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
    }

    /* Allocate the PrefixSum buffers */
    {
#if 1
      splat_tilecount_sbo_ = context_.createBuffer(
        gaussians_count_ * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      );
#else
      gaussians_count_ = kDebugCount; // XXX XXX XXX XXX

      // DEBUG BUFFER
      std::vector<uint32_t> counts(gaussians_count_, 0);


      for (size_t i = 0; (i<kDebugCount) && (i<counts.size()); ++i) {
        counts[i] = 1;
      }

      splat_tilecount_sbo_ = context_.transientCreateBuffer(
        counts,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      );
#endif

      prefix_output_sbo_ = context_.createBuffer(
        gaussians_count_ * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        , VMA_MEMORY_USAGE_GPU_TO_CPU
      );

      // --------------------------------------

      // Hold 1 atomic counter + descriptor flags.
      uint32_t const prefixDescriptorBufferSize = 1u + 2u * vk_utils::GetKernelGridDim(
        gaussians_count_,
        shader_interop::kCompute_PrefixSum_kernelSize_x
      );

      prefix_descriptor_sbo_ = context_.createBuffer(
        prefixDescriptorBufferSize * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
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
      context_.createComputePipelines(pipeline_layout_, shaders, compute_pipelines_.data());
      context_.releaseShaderModules(shaders);
    }

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

      splat_tilecount_sbo_,
      prefix_output_sbo_,
      prefix_descriptor_sbo_
    );
  }

  void dispatchPrefixSum(
    uint32_t const inputSize,
    backend::Buffer const& input,
    backend::Buffer const& output,
    backend::Buffer const& descriptor
  ) {
    if (inputSize == 0) {
      return;
    }

    uint32_t const kKernelSize = shader_interop::kCompute_PrefixSum_kernelSize_x;
    uint32_t const groupCount = vk_utils::GetKernelGridDim(inputSize, kKernelSize);
    VkDeviceSize const tileCounterOffset = groupCount * 2 * sizeof(uint32_t);

    push_constant_.numElems               = inputSize;
    push_constant_.scan_input_addr        = input.address;
    push_constant_.scan_output_addr       = output.address;
    push_constant_.scan_descriptor_addr   = descriptor.address;
    push_constant_.scan_counter_addr      = descriptor.address + tileCounterOffset;

    // ---------------

    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);

    // Clear both descriptor flags and atomic counter.
    cmd.fillBuffer(descriptor, 0u);
    cmd.pipelineBufferBarriers({
      {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                       | VK_ACCESS_2_SHADER_WRITE_BIT
                       ,
        .buffer = descriptor.buffer,
      },
    });

    cmd.bindPipeline(compute_pipelines_[GSCompute_PrefixSum]);
    cmd.pushConstant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);
    cmd.dispatch(groupCount);

    context_.finishTransientCommandEncoder(cmd);
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
    push_constant_.scan_input_addr  = splat_tilecount_sbo_.address;

    // -------------------------------------------

#if 0
    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);
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
      gaussians_count_,
      splat_tilecount_sbo_,
      prefix_output_sbo_,
      prefix_descriptor_sbo_
    );

    // -------------------------------------------1
    if constexpr(false) {
      uint32_t *outputs = nullptr;
      context_.mapMemory(prefix_output_sbo_, &outputs);

      LOGI("> mapping prefix local output");
      for (uint32_t i = 0; i < push_constant_.numElems; ++i) {
        if (i > 0 && (0 == i%shader_interop::kCompute_PrefixSum_kernelSize_x)) {
          fprintf(stderr, "| \n");
        }
        fprintf(stderr, "(%d) %d %s\n",
          i,outputs[i], (i != outputs[i]) ? "X" : ""
        );
      }
      fprintf(stderr, "\n");

      context_.unmapMemory(prefix_output_sbo_);

      exit(-1);
    }
    // -------------------------------------------
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

  backend::Buffer gaussian_sbo_{};          // raw input data
  backend::Buffer splat_sbo_{};             // preprocess output
  backend::Buffer splat_tilecount_sbo_{};   // overlapped tile count.

  backend::Buffer prefix_output_sbo_{}; //
  backend::Buffer prefix_descriptor_sbo_{}; //

  VkPipelineLayout pipeline_layout_{};
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, GSCompute_kCount> compute_pipelines_{};
  uint32_t gaussians_count_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
