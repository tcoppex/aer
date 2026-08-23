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
  let projection = uData.scene.camera.projectionMatrix;
  let view       = uData.scene.camera.viewMatrix;
  let world      = pushConstant.model.worldMatrix;

  let modelViewProj = mul(mul(projection, view), world);

  VSOutput vertex;
  vertex.position = mul(modelViewProj, float4(input.position, 1.0));
  vertex.texCoord = float2(input.texCoord.x, 1.0 - input.texCoord.y);

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  let color = uAlbedoTexture.Sample(input.texCoord).xyz;
  return float4(color, 1.0);
}

// ----------------------------------------------------------------------------