#ifndef SHADERS_SCENE_PBR_METALLIC_ROUGHNESS_INTEROP_H_
#define SHADERS_SCENE_PBR_METALLIC_ROUGHNESS_INTEROP_H_

#include <material/interop.h>
#include <material/push_constant_generic.h>

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

struct PushConstant {
  PushConstant_Generic generic;
};

// ---------------------------------------------------------------------------

#endif // SHADERS_SCENE_PBR_METALLIC_ROUGHNESS_INTEROP_H_
