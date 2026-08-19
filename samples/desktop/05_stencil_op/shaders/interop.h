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
SLANG_STATIC const uint kAttribLocation_Normal   = 1;
SLANG_STATIC const uint kAttribLocation_Texcoord = 2;

SLANG_STATIC const uint kDescriptorSetBinding_UniformBuffer = 0;

SLANG_STATIC const uint kSpecializationConstant_TransformPosition = 0;

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