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

// ---------------------------------------------------------------------------

SLANG_STATIC const uint kDescriptorSetBinding_UniformBuffer    = 0;
SLANG_STATIC const uint kDescriptorSetBinding_Sampler          = 1;
SLANG_STATIC const uint kDescriptorSetBinding_IrradianceEnvMap = 2;

// ---------------------------------------------------------------------------

struct Model {
  float4x4 worldMatrix;
  uint albedo_texture_index;
  uint padding_[3u];
};

// ---------------------------------------------------------------------------

struct Scene {
  float4x4 projectionMatrix;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Scene scene;
};

struct PushConstant {
  Model model;
  float4x4 viewMatrix;
};

// ---------------------------------------------------------------------------

#endif