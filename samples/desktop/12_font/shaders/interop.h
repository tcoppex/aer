#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

const uint kAttribLocation_Position = 0;

const uint kDescriptorSetBinding_UniformBuffer = 0;
const uint kDescriptorSetBinding_Sampler       = 1;

// ---------------------------------------------------------------------------

struct Camera {
  mat4 viewMatrix;
  mat4 projectionMatrix;
};

struct Model {
  mat4 worldMatrix;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Camera camera;
};

struct PushConstant {
  Model model;
};

// ---------------------------------------------------------------------------

#endif