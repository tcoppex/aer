#ifndef SHADERS_SCENE_INTEROP_H_
#define SHADERS_SCENE_INTEROP_H_

#if defined(_GLSL_)

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types : require
#extension GL_EXT_multiview : require

#elif defined(__SLANG__)

typealias mat4 = float4x4;
typealias mat4x3 = float4x3;
typealias vec4 = float4;
typealias vec3 = float3;
typealias vec2 = float2;

#endif

// ----------------------------------------------------------------------------

#if !defined(STATIC_CONST)

#if defined(_GLSL_)
#define STATIC_CONST const
#else
#define STATIC_CONST static const
#endif

#endif

#if defined(__SLANG__)
#define SLANG_PUBLIC public
#else
#define SLANG_PUBLIC
#endif

// ----------------------------------------------------------------------------
// -- Macro helpers --

#if defined(_GLSL_)

#define GetCameraData(frameData, viewIndex) \
  frameData.cameras[viewIndex]

#define GetTexture(texture_id) \
  uTextureChannels[nonuniformEXT(texture_id)]

#elif defined(__SLANG__)

#define GetCameraData(frameData, viewIndex) \
  frameData.cameras[viewIndex]

#define GetTexture(texture_id) \
  uTextureChannels[NonUniformResourceIndex(texture_id)]

#endif

// ----------------------------------------------------------------------------
// -- Vertex Inputs --

STATIC_CONST uint kAttribLocation_Position = 0;
STATIC_CONST uint kAttribLocation_Normal   = 1;
STATIC_CONST uint kAttribLocation_Texcoord = 2;
STATIC_CONST uint kAttribLocation_Tangent  = 3;

SLANG_PUBLIC
struct Vertex {
  SLANG_PUBLIC vec3 position; float _pad0[1];
  SLANG_PUBLIC vec3 normal;   float _pad1[1];
  SLANG_PUBLIC vec4 tangent;
  SLANG_PUBLIC vec2 texcoord; float _pad2[2];
};

// ----------------------------------------------------------------------------
// -- Descriptor Sets --

// set index as used for MaterialFx and bindings as defined in descriptor_registry.

STATIC_CONST uint kDescriptorSet_Internal = 0; // (might be unused)

STATIC_CONST uint kDescriptorSet_Scene = 1;
STATIC_CONST uint kDescriptorSet_Scene_IBL_Prefiltered     = 0;
STATIC_CONST uint kDescriptorSet_Scene_IBL_Irradiance      = 1;
STATIC_CONST uint kDescriptorSet_Scene_IBL_SpecularBRDF    = 2;
STATIC_CONST uint kDescriptorSet_Scene_Textures            = 3; // (must be last to use variable count)

STATIC_CONST uint kDescriptorSet_RayTracing = 2;
STATIC_CONST uint kDescriptorSet_RayTracing_TLAS           = 0;

// ----------------------------------------------------------------------------
// -- Utility structs & constants --

// [hacky] the order *must* match the Camera::Transform struct.
struct CameraData {
  mat4 projectionMatrix;
  mat4 invProjectionMatrix;
  mat4 viewMatrix;
  mat4 invViewMatrix;
  mat4 viewProjMatrix;
};

STATIC_CONST uint kRendererState_IrradianceBit = 0x1 << 0;

// ----------------------------------------------------------------------------
// -- Uniform Buffer(s) --

struct FrameData {
  CameraData cameras[2];
  mat4 default_world_matrix;
  vec4 cameraPos_Time;   // xxx
  vec2 resolution;
  uint frame;
  uint renderer_states; // (wip)
};

// ----------------------------------------------------------------------------
// -- Storage Buffer(s) --

struct TransformData {
  mat4 worldMatrix;
};

// ----------------------------------------------------------------------------

#endif // SHADERS_SCENE_INTEROP_H_
