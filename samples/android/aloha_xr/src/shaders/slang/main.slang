#include "../interop.h"

// ----------------------------------------------------------------------------

struct CameraUBO
{
    UniformCameraData uCameras[2];
};

[[vk::binding(0, 0)]]
ConstantBuffer<CameraUBO> uBO;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput
{
  [[vk::location(0)]] float4 position : POSITION;
  [[vk::location(1)]] float4 color    : COLOR0;
};

struct VSOutput
{
  float4 position : SV_Position;
  float4 color    : COLOR0;
};

// ----------------------------------------------------------------------------
// Vertex Shader
// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint viewIndex : SV_ViewID)
{
  UniformCameraData view = uBO.uCameras[viewIndex];

  float4x4 viewProjMatrix = mul(view.projectionMatrix, view.viewMatrix);
  float4x4 worldMatrix    = pushConstant.modelMatrix;

  VSOutput output;
  output.color    = input.color;
  output.position = mul(mul(viewProjMatrix, worldMatrix), input.position);

  return output;
}

// ----------------------------------------------------------------------------
// Fragment Shader
// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
  return input.color;
}