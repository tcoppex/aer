// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant_Rendering pushConstant;

// ----------------------------------------------------------------------------

struct VSOutput {
  float4 position   : SV_Position;
  float3 normal     : NORMAL;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(
  uint vertexID : SV_VertexID
) {
  Vertex v  = pushConstant.vertices[vertexID];
  let uData = *pushConstant.uniformData;

  let viewMatrix = uData.viewMatrix;
  let projMatrix = uData.projectionMatrix;
  let viewProj   = mul(projMatrix, viewMatrix);
  let MVP        = mul(viewProj, pushConstant.modelMatrix);

  VSOutput output = VSOutput(0);
  output.position = mul(MVP, float4(v.position, 1.0));
  output.normal   = v.normal;
  return output;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  return float4(0.5 * input.normal + 0.5, 1.0);
}

// ----------------------------------------------------------------------------