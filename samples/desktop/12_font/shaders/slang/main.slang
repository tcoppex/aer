// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk_binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  float3 position   : POSITION;
}

struct VSOutput {
  float4 position   : SV_Position;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  const float4x4 projection = uData.camera.projectionMatrix;
  const float4x4 view       = uData.camera.viewMatrix;
  const float4x4 viewProj   = mul(projection, view);
  const float4x4 MVP        = mul(viewProj, pushConstant.model.worldMatrix);

  float3 pos = input.position;

  if (pushConstant.animate) {
    pos.y += 14.0f * sin(pushConstant.instanceID + 3.8f * pushConstant.elapsedTime);
    pos.z += 125.0f * cos(pushConstant.instanceID + 2.1f * pushConstant.elapsedTime);
  }

  VSOutput vertex;
  vertex.position = mul(MVP, float4(pos, 1.0));
  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  return float4(0.76, 0.26, 0.36, 1.0);
}

// ----------------------------------------------------------------------------