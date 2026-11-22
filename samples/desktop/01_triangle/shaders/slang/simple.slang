/* -------------------------------------------------------------------------- */

struct VSOutput {
  float4 position : SV_Position;
  float3 color : COLOR0;
};

// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(
  float4 inPosition : POSITION,
  float4 inColor : COLOR0
) {
  VSOutput vertex;

  vertex.position = inPosition;
  vertex.color = inColor.xyz;

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
  // As the swapchain uses a linear format we have to linearize the input color.
  let color = pow(input.color.rgb, 2.2);
  return float4(color, 1.0f);
}

/* -------------------------------------------------------------------------- */