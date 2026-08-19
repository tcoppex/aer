// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

// A shader buffer act as a view to 1-to-n mapped memory buffers.
// Slang ConstantBuffer are somewhat equivalent to GLSL Uniform buffer.
[[vk_binding(0, 0)]]
ConstantBuffer<UniformData> uData;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  float4 position   : POSITION;
  float3 normal     : NORMAL;
}

struct VSOutput {
  float4 position   : SV_Position;
  float3 normal     : NORMAL;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  const float4x4 projection = uData.scene.camera.projectionMatrix;
  const float4x4 view       = uData.scene.camera.viewMatrix;
  const float4x4 world      = pushConstant.model.worldMatrix;

  float4x4 modelViewProj = mul(mul(projection, view), world);
  float3x3 normalWorld = float3x3(world);

  VSOutput vertex;

  vertex.position = mul(modelViewProj, input.position);
  vertex.normal = normalize(mul(normalWorld, input.normal));

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  const float3 kLightDir = normalize(float3(2.0, 5.0, 4.0));

  float3 nor = normalize(input.normal);
  float3 normalColor = 0.5 * (nor + 1.0);

  // Fake normal, fake light.
  float3 color = smoothstep(-1.2, 1.0, dot(nor, kLightDir)) - normalColor;
         color *= 3.4 + smoothstep(0, 1, dot(nor, float3(0, 0, 1)));
         color *= lerp(color, normalColor, 0.35);

  return float4(color, 1.0);
}

// ----------------------------------------------------------------------------