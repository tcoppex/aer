#ifndef AER_RENDERER_FX_MATERIAL_IMPL_UNLIT_FX_H_
#define AER_RENDERER_FX_MATERIAL_IMPL_UNLIT_FX_H_

#include "aer/renderer/fx/material/material_fx.h"

/* -------------------------------------------------------------------------- */

namespace fx::material {

namespace unlit_shader_interop {
#include "aer/shaders/material/unlit/interop.h"
}

// ----------------------------------------------------------------------------

class UnlitMaterialFx final : public TMaterialFx<unlit_shader_interop::Material> {
 public:
  void set_push_constant_generic(PushConstant_Generic const& data) final {
    push_constant_.generic = data;
  }

 private:
  std::string shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/unlit/scene.frag.glsl";
  }

  std::string vertex_shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/unlit/scene.vert.glsl";
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
      .diffuse_factor = proxy.pbr_mr.basecolor_factor,
      .diffuse_texture_id = proxy.bindings.basecolor,
      .alpha_cutoff = proxy.alpha_cutoff,
      // .double_sided = proxy.double_sided,
    };
  }

 private:
  unlit_shader_interop::PushConstant push_constant_{};
};

}

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_MATERIAL_IMPL_UNLIT_FX_H_
