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

struct VSOutput
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

// ----------------------------------------------------------------------------
// Vertex Shader
// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(
  uint vertexID : SV_VertexID,
  uint viewIndex : SV_ViewID
)
{
  float2 positions[3] = {
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0)
  };

  float2 uvs[3] = {
    float2(0.0, 1.0),
    float2(2.0, 1.0),
    float2(0.0, -1.0)
  };

  UniformCameraData view = uBO.uCameras[viewIndex];

  VSOutput output;
  output.position = float4(positions[vertexID], 0.0, 1.0);
  output.uv = uvs[vertexID];

  return output;
}

// ----------------------------------------------------------------------------
// Fragment Shader
// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
  return float4(0.4, 1.0, 0.8, 0.75);
}