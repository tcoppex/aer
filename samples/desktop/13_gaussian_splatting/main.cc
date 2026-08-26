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

class SampleApp final : public Application {
  public:
  enum GSCompute {
    GSCompute_Preprocess = 0,

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

    /* Create the Compute Pipelines */
    {
      pipeline_layout_ = context_.createPipelineLayout({
        // .setLayouts = { descriptor_set_layout_ },
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .size = sizeof(push_constant_),
          }
        },
      });

      auto shaders = context_.createShaderModules(SAMPLE_SPIRV_DIR, {
        "gaussian_preprocess.slang",
      });

      context_.createComputePipelines(
        pipeline_layout_,
        ShaderStageDescriptors{{ shaders[GSCompute_Preprocess] }},
        compute_pipelines_.data()
      );

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
      gaussian_sbo_,
      splat_sbo_
    );
  }

  void update(float const dt) final {
    // (probably change)
    push_constant_.viewMatrix       = camera_.view();
    push_constant_.position         = float4(camera_.position(), 0.0f);

    // (probably does not change)
    push_constant_.projectionMatrix = camera_.proj();
    push_constant_.focal            = camera_.focals();
    push_constant_.tanFov           = camera_.tan_fovs();
    push_constant_.resolution       = float2(
      static_cast<float>(camera_.width()),
      static_cast<float>(camera_.height())
    );
    push_constant_.gaussian_addr_   = gaussian_sbo_.address;
    push_constant_.splat_addr_      = splat_sbo_.address;
  }

  void draw(CommandEncoder const& cmd) final {
    push_constant_.numElems = gaussians_count_;
    cmd.pushConstant(
      push_constant_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT
    );

    cmd.bindPipeline(compute_pipelines_[GSCompute_Preprocess]);
    cmd.dispatch<shader_interop::kCompute_Preprocess_kernelSize_x>(
      gaussians_count_
    );

    // cmd.pipelineBufferBarriers({});

    auto pass = cmd.beginRendering();
    {
    }
    cmd.endRendering();

    drawUI(cmd);
  }

  void buildUI() final {}

 private:
  ArcBallController arcball_controller_{};

  backend::Buffer gaussian_sbo_{};
  backend::Buffer splat_sbo_{};

  VkPipelineLayout pipeline_layout_{};
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, GSCompute_kCount> compute_pipelines_{};
  uint32_t gaussians_count_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
