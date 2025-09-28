#ifndef SHADERS_SCENE_UNLIT_H_
#define SHADERS_SCENE_UNLIT_H_

// ---------------------------------------------------------------------------

const uint kDescriptorSet_Internal_MaterialSBO     = 0;
const uint kDescriptorSetBinding_TransformSBO      = 1;

struct Material {
  vec4 diffuse_factor;
};

struct PushConstant {
  uint transform_index;
  uint material_index;
  uint instance_index;
  uint padding_[1];
};

// ---------------------------------------------------------------------------

#endif
