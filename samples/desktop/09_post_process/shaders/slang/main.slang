// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[[vk::binding(kDescriptorSetBinding_Scene_Textures, 0)]]
Sampler2D uTextureChannels[];

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  float3 position   : POSITION;
  float3 normal     : NORMAL;
  float2 texCoord   : TEXCOORD0;
};

struct VSOutput {
  float4 position   : SV_Position;
  float3 positionWS : POSITION;
  float2 texCoord   : TEXCOORD0;
  float3 normal     : NORMAL;
};

struct FSOutput {
  float4 color      : SV_Target0;
  float4 data       : SV_Target1;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  let projection = uData.scene.projectionMatrix;
  let view       = pushConstant.viewMatrix;
  let world      = pushConstant.model.worldMatrix;

  let worldPos = mul(world, float4(input.position, 1.0));

  VSOutput vertex;
  vertex.position   = mul(mul(projection, view), worldPos);
  vertex.positionWS = worldPos.xyz;
  vertex.normal     = normalize(mul(float3x3(world), input.normal));
  vertex.texCoord   = input.texCoord;

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
FSOutput fragmentMain(VSOutput input) {
  let diffuse = uTextureChannels[pushConstant.model.albedo_texture_index]
    .Sample(input.texCoord);

  if (diffuse.a < 0.499) {
    discard;
  }
  let depth = length(input.positionWS - pushConstant.cameraPosition);

  FSOutput output;
  output.color = diffuse;
  output.data.xy  = encodeNormal(normalize(input.normal));
  output.data.z   = depth;
  output.data.w   = asfloat(asint(pushConstant.model.instance_index) + 1);

  return output;
}

float2 encodeNormal(in float3 n) {
  let p = sqrt(n.z * 8.0 + 8.0);
  return float2(n.xy / p + 0.5);
}

// ----------------------------------------------------------------------------
