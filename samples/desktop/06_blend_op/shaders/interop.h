#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

#define STATIC_CONST static const

// ---------------------------------------------------------------------------

STATIC_CONST uint kAttribLocation_Position = 0;

STATIC_CONST uint kDescriptorSetBinding_UniformBuffer          = 0;
STATIC_CONST uint kDescriptorSetBinding_StorageBuffer_Position = 1;
STATIC_CONST uint kDescriptorSetBinding_StorageBuffer_Index    = 2;

// ---------------------------------------------------------------------------

struct Camera {
  float4x4 viewMatrix;
  float4x4 projectionMatrix;
};

struct Model {
  float4x4 worldMatrix;
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
  float time;
};

// ---------------------------------------------------------------------------

#endif