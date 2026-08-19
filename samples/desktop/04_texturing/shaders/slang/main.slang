// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[vk::binding(kDescriptorSetBinding_Sampler, 0)]
Sampler2D uAlbedoTexture;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  float3 position   : POSITION;
  float3 normal     : NORMAL;
  float2 texCoord   : TEXCOORD0;
}

struct VSOutput {
  float4 position   : SV_Position;
  float2 texCoord   : TEXCOORD0;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  const float4x4 projection = uData.scene.camera.projectionMatrix;
  const float4x4 view       = uData.scene.camera.viewMatrix;
  const float4x4 world      = pushConstant.model.worldMatrix;

  float4x4 modelViewProj = mul(mul(projection, view), world);

  VSOutput vertex;

  vertex.position = mul(modelViewProj, float4(input.position, 1.0));
  vertex.texCoord = float2(input.texCoord.x, 1.0 - input.texCoord.y);

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  float3 color = uAlbedoTexture.Sample(input.texCoord).xyz;
  return float4(color, 1.0);
}

// ----------------------------------------------------------------------------