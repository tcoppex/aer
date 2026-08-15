#ifndef SHADERS_SCENE_UNLIT_H_
#define SHADERS_SCENE_UNLIT_H_

#include <material/interop.h>
#include <material/push_constant_generic.h>

// ---------------------------------------------------------------------------

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
