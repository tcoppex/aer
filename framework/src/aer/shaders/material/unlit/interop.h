#ifndef SHADERS_SCENE_UNLIT_H_
#define SHADERS_SCENE_UNLIT_H_

#include <material/interop.h>
#include <material/push_constant_generic.h>

// ---------------------------------------------------------------------------

const uint kDescriptorSet_Internal_MaterialSBO     = 0;
const uint kDescriptorSetBinding_TransformSBO      = 1;

struct Material {
  vec4 diffuse_factor;
  uint diffuse_texture_id;
  float alpha_cutoff;
  uint pad0_[2];
};

struct PushConstant {
  PushConstant_Generic generic;
};

// ---------------------------------------------------------------------------

#endif
