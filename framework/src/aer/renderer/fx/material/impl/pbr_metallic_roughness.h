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
  void setup() final {
    TMaterialFx<PBRMetallicRoughnessMaterial>::setup();

    context_ptr_->update_descriptor_set(descriptor_set_, {
      {
        .binding = pbr_metallic_roughness_shader_interop::kDescriptorSet_Internal_MaterialSBO,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffers = { { material_storage_buffer_.buffer } },
      },
    });
  }

  void release() final {
    TMaterialFx<PBRMetallicRoughnessMaterial>::release();
  }

 public:
  void setTransformIndex(uint32_t index) final {
    push_constant_.transform_index = index;
  }

  void setMaterialIndex(uint32_t index) final {
    push_constant_.material_index = index;
  }

  void setInstanceIndex(uint32_t index) final {
    push_constant_.instance_index = index;
  }

 private:
  std::string getShaderName() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/pbr_metallic_roughness/scene.frag.glsl";
  }

  std::string getVertexShaderName() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "material/pbr_metallic_roughness/scene.vert.glsl";
  }

  DescriptorSetLayoutParamsBuffer getDescriptorSetLayoutParams() const final {
    return {
      {
        .binding = pbr_metallic_roughness_shader_interop::kDescriptorSet_Internal_MaterialSBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT,
      },
    };
  }

  std::vector<VkPushConstantRange> getPushConstantRanges() const final {
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
    cmd.push_constant(
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

}

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_MATERIAL_IMPL_PBR_METALLIC_ROUGHNESS_H_
