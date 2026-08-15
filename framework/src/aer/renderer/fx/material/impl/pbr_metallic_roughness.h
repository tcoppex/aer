#ifndef AER_RENDERER_FX_MATERIAL_IMPL_PBR_METALLIC_ROUGHNESS_H_
#define AER_RENDERER_FX_MATERIAL_IMPL_PBR_METALLIC_ROUGHNESS_H_

#include "aer/renderer/fx/material/material_fx.h"

/* -------------------------------------------------------------------------- */

namespace fx::material {

namespace pbr_metallic_roughness_shader_interop {
#include "aer/shaders/material/pbr_metallic_roughness/interop.h"
}

using PBRMetallicRoughnessMaterial = pbr_metallic_roughness_shader_interop::Material;

// ----------------------------------------------------------------------------

class PBRMetallicRoughnessFx final : public TMaterialFx<PBRMetallicRoughnessMaterial> {
 public:
  void set_push_constant_generic(PushConstant_Generic const& data) final {
    push_constant_.generic = data;
  }

 private:
  std::string shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/pbr_metallic_roughness/scene.frag.glsl";
  }

  std::string vertex_shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/pbr_metallic_roughness/scene.vert.glsl";
  }

  std::vector<VkPushConstantRange> push_constant_ranges() const final {
    return {
      {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    ,
        .size = sizeof(push_constant_),
      }
    };
  }

  void pushConstant(GenericCommandEncoder const &cmd) final {
    cmd.pushConstant(
      push_constant_,
      pipeline_layout_,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    );
  }

 private:
  ShaderMaterial convertMaterialProxy(scene::MaterialProxy const& proxy) const final {
    return {
      .emissive_factor = proxy.emissive_factor,
      .emissive_texture_id = proxy.bindings.emissive,
      .diffuse_factor = proxy.pbr_mr.basecolor_factor,
      .diffuse_texture_id = proxy.bindings.basecolor,
      .orm_texture_id = proxy.bindings.roughness_metallic,
      .metallic_factor = proxy.pbr_mr.metallic_factor,
      .roughness_factor = proxy.pbr_mr.roughness_factor,
      .normal_texture_id = proxy.bindings.normal,
      .occlusion_texture_id = proxy.bindings.occlusion,
      .alpha_cutoff = proxy.alpha_cutoff,
      .double_sided = proxy.double_sided,
    };
  }

 private:
  pbr_metallic_roughness_shader_interop::PushConstant push_constant_{};
};

} // namespace fx::material

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_MATERIAL_IMPL_PBR_METALLIC_ROUGHNESS_H_
