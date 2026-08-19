// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::constant_id(kSpecializationConstant_TransformPosition)]]
constexpr bool sc_transformPosition = false;

[[vk::binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  uint   instanceID : SV_InstanceID;
  float3 position   : POSITION;
  float3 normal     : NORMAL;
  float2 texCoord   : TEXCOORD0;
};

struct VSOutput {
  float4 position   : SV_Position;
  float3 color      : COLOR;
  float2 texCoord   : TEXCOORD0;
};

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

float3 transformPosition(float3 position, uint instanceID) {
  const float di = instanceID / 256.0;
  const float ystep = 0.092f;
  const float scale = (1.0 - 0.65 * di);

  float3 offset = float3(
    0.14 * sin(13.4 * di * 6.283),
    -(2.0 + instanceID) * ystep,
    0.13 * cos(17.0 * di * 6.283)
  );

  return offset + float3(scale, 1.0, scale) * position;
}

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  const float4x4 projection = uData.scene.camera.projectionMatrix;
  const float4x4 view       = uData.scene.camera.viewMatrix;
  const float4x4 world      = pushConstant.model.worldMatrix;

  const float4x4 modelViewProj = mul(mul(projection, view), world);
  const float3 normal = normalize(mul(float3x3(world), input.normal));

  float3 pos = input.position;

  // Instanced "internal space" rings change their transform based on their ID.
  if (sc_transformPosition) {
    pos = transformPosition(pos, input.instanceID);
  }

  VSOutput vertex;
  vertex.position = mul(modelViewProj, float4(pos, 1.0));
  vertex.color    = normal * 0.5 + 0.5;
  vertex.texCoord = input.texCoord;

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  return float4(input.color, 1.0);
}

// ----------------------------------------------------------------------------
