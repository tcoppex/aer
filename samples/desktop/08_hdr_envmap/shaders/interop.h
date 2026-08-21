#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

#if defined(_GLSL_)

#define STATIC_CONST const
#define float4x4 mat4 //

#else

#define STATIC_CONST static const
// typealias mat4 = float4x4;

#endif

// ---------------------------------------------------------------------------

STATIC_CONST uint kAttribLocation_Position = 0;
STATIC_CONST uint kAttribLocation_Normal   = 1;
STATIC_CONST uint kAttribLocation_Texcoord = 2;

// ---------------------------------------------------------------------------

STATIC_CONST uint kDescriptorSetBinding_UniformBuffer    = 0;
STATIC_CONST uint kDescriptorSetBinding_Sampler          = 1;
STATIC_CONST uint kDescriptorSetBinding_IrradianceEnvMap = 2;

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