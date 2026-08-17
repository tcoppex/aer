#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

const uint kAttribLocation_Position = 0;
const uint kAttribLocation_Normal   = 1;
const uint kAttribLocation_Texcoord = 2;

// ---------------------------------------------------------------------------

const uint kDescriptorSetBinding_UniformBuffer    = 0;
const uint kDescriptorSetBinding_Sampler          = 1;
const uint kDescriptorSetBinding_IrradianceEnvMap = 2;

// ---------------------------------------------------------------------------

struct Model {
  mat4 worldMatrix;
  uint albedo_texture_index;
  uint padding_[3u];
};

// ---------------------------------------------------------------------------

struct Scene {
  mat4 projectionMatrix;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Scene scene;
};

struct PushConstant {
  Model model;
  mat4 viewMatrix;
};

// ---------------------------------------------------------------------------

#endif