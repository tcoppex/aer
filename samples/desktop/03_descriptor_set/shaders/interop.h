#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// [deprecated, to be used by the GLSL shaders]
#if defined(_GLSL_)
#define float4x4 mat4
#endif

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