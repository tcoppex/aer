// ----------------------------------------------------------------------------
// Skeleton overlay: colored world space lines.
// ----------------------------------------------------------------------------

struct PushConstant_t {
  float4x4 viewProjection;
};

[[vk::push_constant]]
PushConstant_t pc;

struct VSInput {
  [[vk::location(0)]]
  float3 position : POSITION;

  [[vk::location(1)]]
  float3 color    : COLOR;
};

struct VSOutput {
  float4 position : SV_Position;

  [[vk::location(0)]]
  float3 color    : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  VSOutput output;
  output.position = mul(pc.viewProjection, float4(input.position, 1.0));
  output.color    = input.color;
  return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
  return float4(input.color, 1.0);
}