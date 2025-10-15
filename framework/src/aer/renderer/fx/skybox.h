#ifndef AER_RENDERER_FX_SKYBOX_H_
#define AER_RENDERER_FX_SKYBOX_H_

#include "aer/core/common.h"

#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/pipeline.h"
#include "aer/scene/mesh.h"
#include "aer/renderer/fx/envmap.h"

namespace shader_interop::skybox {
#include "aer/shaders/skybox/interop.h" //
}

class Context;
class Renderer;
class Camera;

/* -------------------------------------------------------------------------- */

class Skybox {
 public:
  static constexpr uint32_t kBRDFLutResolution{ 512u };

 public:
  Skybox() = default;

  void init(Renderer& renderer); //

  void release(RenderContext const& context);

  bool setup(std::string_view hdr_filename); //

  void render(RenderPassEncoder& pass, Camera const& camera) const;

  Envmap const& envmap() const {
    return envmap_;
  }

  backend::Image const& specular_brdf_lut() const {
    return specular_brdf_lut_;
  }

  VkSampler const& sampler() const {
    return sampler_LinearClampMipMap_;
  }

  backend::Image const& prefiltered_specular_map() const {
    return envmap_.get_image(Envmap::ImageType::Specular);
  }

  backend::Image const& irradiance_map() const {
    return envmap_.get_image(Envmap::ImageType::Irradiance);
  }

  bool is_valid() const {
    return setuped_;
  }

 private:
  void compute_specular_brdf_lut(Renderer const& renderer);

 private:
  using PushConstant_t = shader_interop::skybox::PushConstant;

  Renderer const* renderer_ptr_{};
  Envmap envmap_{};

  backend::Image specular_brdf_lut_{};
  VkSampler sampler_LinearClampMipMap_{};

  scene::Mesh cube_{};
  backend::Buffer vertex_buffer_{};
  backend::Buffer index_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};

  VkPipelineLayout pipeline_layout_{};
  Pipeline graphics_pipeline_{};

  bool setuped_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_SKYBOX_H_
