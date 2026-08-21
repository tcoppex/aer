#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

#if defined(_GLSL_)

#define float4x4 mat4
#define STATIC_CONST const

#else

#define STATIC_CONST static const

#endif

// ---------------------------------------------------------------------------

STATIC_CONST uint kAttribLocation_Position = 0;

STATIC_CONST uint kDescriptorBinding_UBO_Data = 0;
STATIC_CONST uint kDescriptorBinding_SBO_Positions    = 1;
STATIC_CONST uint kDescriptorBinding_SBO_Indices      = 2;
STATIC_CONST uint kDescriptorBinding_SBO_DotProducts  = 3;

STATIC_CONST uint kCompute_Simulation_kernelSize_x  = 256;
STATIC_CONST uint kCompute_FillIndex_kernelSize_x   = 256;
STATIC_CONST uint kCompute_DotProduct_kernelSize_x  = 256;
STATIC_CONST uint kCompute_SortIndex_kernelSize_x   = 256;

STATIC_CONST float kTwoPi = 6.28318530718f;

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

struct PushConstant_Graphics {
  Model model;
};

struct PushConstant_Compute {
  Model model;
  float time;
  uint numElems;
  uint padding_[2];
  uint readOffset;
  uint writeOffset;
  uint blockWidth;
  uint maxBlockWidth;
};

// ---------------------------------------------------------------------------

struct PushConstant {
  PushConstant_Graphics graphics;
  PushConstant_Compute compute;
};

// ---------------------------------------------------------------------------

#endif