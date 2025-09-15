#ifndef SHADERS_SCENE_PBR_METALLIC_ROUGHNESS_INTEROP_H_
#define SHADERS_SCENE_PBR_METALLIC_ROUGHNESS_INTEROP_H_

// ---------------------------------------------------------------------------

const uint kDescriptorSet_Internal_MaterialSSBO     = 0;
const uint kDescriptorSetBinding_TransformSSBO      = 1;

// ---------------------------------------------------------------------------
// Fx Materials SSBOs struct.

struct Material {
  vec3 emissive_factor;
  uint emissive_texture_id;
  vec4 diffuse_factor;
  uint diffuse_texture_id;
  uint orm_texture_id;
  float metallic_factor;
  float roughness_factor;

  uint normal_texture_id;
  uint occlusion_texture_id;

  float alpha_cutoff;
  bool double_sided;
};

// ---------------------------------------------------------------------------
// Instance PushConstants.

// [80 bytes < 128 bytes]
struct PushConstant {
  uint transform_index;
  uint material_index;
  uint instance_index;
  uint dynamic_states;
};

const uint kIrradianceBit = 0x1 << 0; //

// ---------------------------------------------------------------------------

#endif
