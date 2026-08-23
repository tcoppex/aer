// ----------------------------------------------------------------------------

[[vk::binding(0, 0)]]
Sampler2D inChannels[];

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 main(
  float2 texcoord : TEXCOORD0
) : SV_Target0 {
  var color    = inChannels[0].Sample(texcoord);
  color.xyz = lerp(grayscale(color.xyz), float3(0.88, 0.88, 0.77), 0.8);

  let edge_nor = inChannels[1].Sample(texcoord).r;
  let edge_obj = inChannels[2].Sample(texcoord).r;
  let edge_color = (float3(1) - color.xyz * 0.8);

  color.xyz = lerp(color.xyz, edge_color, edge_nor);
  color.xyz = lerp(color.xyz, edge_color, edge_obj);

  return color;
}

float3 grayscale(float3 color) {
  return float3(dot(color, float3(0.299, 0.587, 0.114)));
}

// ----------------------------------------------------------------------------
