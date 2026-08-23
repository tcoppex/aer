#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ----------------------------------------------------------------------------

#if !defined(STATIC_CONST)

#if defined(_GLSL_)
#define STATIC_CONST const
#else
#define STATIC_CONST static const
#endif

#endif

#if defined(__SLANG__)
  typealias mat4 = float4x4;
typealias mat4x3 = float4x3;
typealias vec4 = float4;
typealias vec3 = float3;
typealias vec2 = float2;
#endif

// ----------------------------------------------------------------------------

STATIC_CONST uint kAttribLocation_Position = 0;
STATIC_CONST uint kAttribLocation_Normal   = 1;
STATIC_CONST uint kAttribLocation_Texcoord = 2;

// ----------------------------------------------------------------------------

STATIC_CONST uint kDescriptorSetBinding_UniformBuffer    = 0;
STATIC_CONST uint kDescriptorSetBinding_Scene_Textures   = 1;

// ----------------------------------------------------------------------------

struct Model {
  mat4 worldMatrix;
  uint albedo_texture_index;
  uint material_index;
  uint instance_index;
  uint padding_[1];
};

// ----------------------------------------------------------------------------

struct Scene {
  mat4 projectionMatrix;
};

// ----------------------------------------------------------------------------

struct UniformData {
  Scene scene;
};

struct PushConstant {
  Model model;
  mat4 viewMatrix;
  vec3 cameraPosition;
  uint padding;
};

// ----------------------------------------------------------------------------

#endif