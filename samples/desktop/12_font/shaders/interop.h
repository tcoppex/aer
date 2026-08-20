#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

#if defined(_GLSL_)

#define float4x4 mat4
#define SLANG_STATIC

#else

#define SLANG_STATIC static

#endif

// ---------------------------------------------------------------------------

SLANG_STATIC const uint kAttribLocation_Position = 0;

SLANG_STATIC const uint kDescriptorSetBinding_UniformBuffer = 0;
SLANG_STATIC const uint kDescriptorSetBinding_Sampler       = 1;

// ---------------------------------------------------------------------------

struct Camera {
  float4x4 viewMatrix;
  float4x4 projectionMatrix;
};

struct Model {
  float4x4 worldMatrix;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Camera camera;
};

struct PushConstant {
  float elapsedTime;
  bool animate;
  uint instanceID;
  uint _pad0[1];
  Model model;
};

// ---------------------------------------------------------------------------

#endif