#ifndef SHADERS_INTEROP_RENDERING_H_
#define SHADERS_INTEROP_RENDERING_H_

#include "chunk_constants.h" //

// ----------------------------------------------------------------------------

struct UniformBufferData {
  float4x4 viewMatrix;
  float4x4 projectionMatrix;
};

struct PushConstant {
  float4x4 modelMatrix;
  UniformBufferData* uniformData;
  MarchingCubeVertex* vertices;
  uint32_t vertexOffset;
};

// ---------------------------------------------------------------------------

#endif // SHADERS_INTEROP_RENDERING_H_
