#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

const uint kAttribLocation_Position = 0;
const uint kAttribLocation_Normal   = 1;
const uint kAttribLocation_Texcoord = 2;

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

struct Scene {
  Camera camera;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Scene scene;
};

struct PushConstant {
  Model model;
};

// ---------------------------------------------------------------------------

#endif