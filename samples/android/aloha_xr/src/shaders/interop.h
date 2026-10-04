#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

#if defined(_GLSL_)
#define float4x4 mat4
#endif

// ---------------------------------------------------------------------------

struct UniformCameraData {
  float4x4 projectionMatrix;
  float4x4 viewMatrix;
};

struct PushConstant {
  float4x4 modelMatrix;
};

// ---------------------------------------------------------------------------

#endif