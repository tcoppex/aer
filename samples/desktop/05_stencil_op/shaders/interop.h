#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

#define STATIC_CONST static const

// ---------------------------------------------------------------------------

STATIC_CONST uint kAttribLocation_Position = 0;
STATIC_CONST uint kAttribLocation_Normal   = 1;
STATIC_CONST uint kAttribLocation_Texcoord = 2;

STATIC_CONST uint kDescriptorSetBinding_UniformBuffer = 0;

STATIC_CONST uint kSpecializationConstant_TransformPosition = 0;

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
};

// ---------------------------------------------------------------------------

#endif