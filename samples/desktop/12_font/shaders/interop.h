#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------

static const uint kAttribLocation_Position = 0;

static const uint kDescriptorSetBinding_UniformBuffer = 0;
static const uint kDescriptorSetBinding_Sampler       = 1;

// ---------------------------------------------------------------------------

struct Camera {
  float4x4 viewMatrix;
  float4x4 projectionMatrix;
};

// ---------------------------------------------------------------------------

struct UniformData {
  Camera camera;
  float4 fillColor;
  // ----------------
  float4 outlineColor;
  float2 atlasSize;
  float pxRange;
  float outlineWidth;
};

struct PushConstant {
  float4x4 worldMatrix;
  float elapsedTime;
  bool animate;
  uint instanceID;
  uint _pad0[1];
  // ----------------
  float4 rect;  // xy = bottom-left (em), zw = size (em)
  float4 uv;    // (left, right, top, bottom)
};

// ---------------------------------------------------------------------------

#endif