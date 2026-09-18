// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant_Rendering pushConstant;

// ----------------------------------------------------------------------------

struct VSOutput {
  float4 position   : SV_Position;
  float3 normal     : NORMAL;
  float ao;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(
  uint vertexID : SV_VertexID,
  uint drawID   : SV_DrawIndex
) {
  let uData = *pushConstant.uniformData;
  let viewMatrix = uData.viewMatrix;
  let projMatrix = uData.projectionMatrix;
  let viewProj   = mul(projMatrix, viewMatrix);
  let MVP        = mul(viewProj, pushConstant.modelMatrix);

  let offset = pushConstant.vertexOffset * drawID;

  let input  = pushConstant.vertices[offset + vertexID];
  var output = VSOutput(0);

  output.position = mul(MVP, float4(input.position, 1.0));
  output.normal   = input.normal;
  output.ao       = input.ao;

  return output;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  var color = float4(0.5 * input.normal + 0.5, 1.0);

  color.rgb *= input.ao;

  return color;
}

// ----------------------------------------------------------------------------