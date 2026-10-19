#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

struct UniformCameraData {
  float4x4 projectionMatrix;
  float4x4 viewMatrix;
};

struct PushConstant {
  float4x4 modelMatrix;
  float audioAmplitude;
  float audioMids;
  float audioHighs;
  float time;
};

// ---------------------------------------------------------------------------

#endif