#ifndef AER_RENDERER_FX_ENVMAP_H_
#define AER_RENDERER_FX_ENVMAP_H_

#include "aer/core/common.h"
#include "aer/core/utils.h"

#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/pipeline.h"

namespace shader_interop::envmap {
#include "aer/shaders/envmap/interop.h"
}

class RenderContext;
class Renderer;

/* -------------------------------------------------------------------------- */

class Envmap {
 public:
  static uint32_t constexpr kFaceCount{ 6u };

  static constexpr uint32_t kDiffuseResolution{ 1024u };
  static constexpr uint32_t kIrradianceResolution{ 128u };
  static constexpr uint32_t kSpecularResolution{ 256u };

  static constexpr uint32_t kSpecularSampleCount{ 64u };
  static constexpr uint32_t kSpecularLevelCount{
    utils::Log2_u32(kSpecularResolution)
  };
  static float constexpr kInvMaxSpecularLevel{
    (kSpecularLevelCount <= 1u) ? 1.0f : 1.0f / static_cast<float>(kSpecularLevelCount - 1u)
  };

 public:
  enum class ImageType {
    Diffuse,
    Irradiance,
    Specular,

    kCount
  };

  enum class ComputeStage {
    TransformSpherical,
    IrradianceSHCoeff,
    ReduceSHCoeff,
    IrradianceTransfer,
    Irradiance,
    Specular,

    kCount,
  };

 public:
  Envmap() = default;

  void init(RenderContext const& context);

  void release();

  bool setup(std::string_view filename);

  backend::Image const& get_image(ImageType const type) const {
    return images_[type];
  }

  backend::Buffer const& get_irradiance_matrices_buffer() const {
    return irradiance_matrices_buffer_;
  }

 private:
  bool load_diffuse_envmap(std::string_view filename);

  void compute_irradiance_sh_coeff();

  void compute_irradiance();

  void compute_specular();

 private:
  RenderContext const* context_ptr_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::envmap::PushConstant push_constant_{};

  VkPipelineLayout pipeline_layout_{};
  EnumArray<Pipeline, ComputeStage> compute_pipelines_{};

  VkSampler sampler_{}; //
  EnumArray<backend::Image, ImageType> images_{};

  backend::Buffer irradiance_matrices_buffer_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_ENVMAP_H_
