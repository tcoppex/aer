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
    void setup() final {
    TMaterialFx<unlit_shader_interop::Material>::setup();

    context_ptr_->updateDescriptorSet(descriptor_set_, {
      {
        .binding = unlit_shader_interop::kDescriptorSet_Internal_MaterialSBO,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffers = { { material_storage_buffer_.buffer } },
      },
    });
  }

 public:
  void set_transform_index(uint32_t index) final {
    push_constant_.transform_index = index;
  }

  void set_material_index(uint32_t index) final {
    push_constant_.material_index = index;
  }

  void set_instance_index(uint32_t index) final {
    push_constant_.instance_index = index;
  }

 private:
  std::string shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/unlit/scene.frag.glsl";
  }

  std::string vertex_shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/unlit/scene.vert.glsl";
  }

  DescriptorSetLayoutParamsBuffer descriptor_set_layout_params() const final {
    return {
      {
        .binding = unlit_shader_interop::kDescriptorSet_Internal_MaterialSBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                      | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      },
    };
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
    cmd.pushConstant(push_constant_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
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
