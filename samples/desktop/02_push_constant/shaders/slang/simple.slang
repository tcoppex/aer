// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  float4 position   : POSITION;
  float4 color      : COLOR0;
}

struct VSOutput {
  float4 position   : SV_Position;
  float4 color      : COLOR0;
};

// ----------------------------------------------------------------------------

float3 rotateZ(float3 pt, float theta) {
  float c = cos(theta);
  float s = sin(theta);
  return float3(
    dot(pt.xy, float2(c, -s)),
    dot(pt.xy, float2(s, c)),
    pt.z
  );
}

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  VSOutput vertex;

  float3 pos = rotateZ(input.position.xyz, pushConstant.time);

  vertex.position = float4(pos, 1.0f);
  vertex.color = input.color;

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
  return input.color;
}

// ----------------------------------------------------------------------------