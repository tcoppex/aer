// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[vk::binding(kDescriptorSetBinding_Sampler, 0)]
Sampler2D uTextures[];

[vk::binding(kDescriptorSetBinding_IrradianceEnvMap, 0)]
SamplerCube uIrradianceEnvMap;

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
  float3 normal     : NORMAL;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  const float4x4 projection = uData.scene.projectionMatrix;
  const float4x4 view       = pushConstant.viewMatrix;
  const float4x4 world      = pushConstant.model.worldMatrix;

  float4x4 modelViewProj = mul(mul(projection, view), world);

  VSOutput vertex;

  vertex.position = mul(modelViewProj, float4(input.position, 1.0));
  vertex.texCoord = input.texCoord;
  vertex.normal   = normalize(mul(float3x3(world), input.normal));

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  // Sample the albedo texture.
  const uint texid = pushConstant.model.albedo_texture_index;
  float4 color = uTextures[texid].Sample(input.texCoord);

  // Sample the environment irradiance map.
  float3 irradiance = uIrradianceEnvMap.Sample(input.normal).rgb;

  // Compose them together.
  color.rgb *= 0.5 * irradiance;

  return color;
}

// ----------------------------------------------------------------------------