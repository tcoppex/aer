// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorBinding_UBO_Data, 0)]]
ConstantBuffer<UniformData> uData;

[[vk::binding(kDescriptorBinding_SBO_Positions, 0)]]
StructuredBuffer<float4> Positions;

[[vk::binding(kDescriptorBinding_SBO_Indices, 0)]]
StructuredBuffer<uint> Indices;

[[vk::push_constant]]
PushConstant_Graphics pushConstant;

struct VSInput {
  uint instanceID   : SV_InstanceID;
  uint vertexID     : SV_VertexID;
};

struct VSOutput {
  float4 position   : SV_Position;

  [[vk::location(0)]]
  float2 texCoord   : TEXCOORD0;

  [[vk::location(1)]]
  float3 positionWS : POSITION;
};

// ----------------------------------------------------------------------------

static const float2 kLocalOffsets[4] = {
  float2(-0.5,  0.5),
  float2(-0.5, -0.5),
  float2( 0.5,  0.5),
  float2( 0.5, -0.5)
};

static const float2 kLocalTexCoords[4] = {
  float2(0.0, 1.0),
  float2(0.0, 0.0),
  float2(1.0, 1.0),
  float2(1.0, 0.0)
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  const float4x4 projection = uData.scene.camera.projectionMatrix;
  const float4x4 view       = uData.scene.camera.viewMatrix;
  const float4x4 world      = pushConstant.model.worldMatrix;

  const float4x4 viewProj = mul(projection, view);

  // --------------------

  const float4 particle = Positions[Indices[input.instanceID]];
  const float3 center   = particle.xyz;
  const float2 scale    = float2(particle.w * 0.15f);

  const float2 quad_offset = kLocalOffsets[input.vertexID] * scale;

  /* Create a 'billboard' quad from the particle position. */
  // Important: in Slang, matrices accessors are always row major (different than)
  const float3 quad_right = normalize(float3(view[0][0], view[0][1], view[0][2]));
  const float3 quad_up    = normalize(float3(view[1][0], view[1][1], view[1][2]));

  const float3 vertex_offset = quad_offset.x * quad_right
                             + quad_offset.y * quad_up
                             ;

  const float3 worldPos = mul(world, float4(center, 1.0)).xyz
                        + vertex_offset
                        ;

  // --------------------

  VSOutput vertex = VSOutput(0);

  vertex.position   = mul(viewProj, float4(worldPos, 1.0));
  vertex.positionWS = worldPos;
  vertex.texCoord   = kLocalTexCoords[input.vertexID];

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  const float dLimit = 207.0;
  const float dRange = 5.0;

  float d = dot(input.positionWS, input.positionWS);
  float borderAlpha = 1.0 - smoothstep(dLimit - dRange, dLimit + dRange, d);

  float2 pt = 2.0f * (input.texCoord - 0.5f);
  float l = 1.0f - abs(dot(pt, pt));

  float alpha = borderAlpha * smoothstep(0.65f, 0.85f, l);

  float4 color = float4(float3(l), alpha);

  color *= float4(
    0.15,
    0.70  * (0.75 + 0.35 * input.positionWS.y),
    0.95 + 0.05 * input.positionWS.y,
    0.25
  );
  color *= l;

  return color;
}

// ----------------------------------------------------------------------------
