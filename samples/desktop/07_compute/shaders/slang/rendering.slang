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
  let projection = uData.scene.camera.projectionMatrix;
  let view       = uData.scene.camera.viewMatrix;
  let world      = pushConstant.model.worldMatrix;

  let viewProj   = mul(projection, view);

  // --------------------

  let particle = Positions[Indices[input.instanceID]];
  let center   = particle.xyz;
  let scale    = float2(particle.w * 0.15f);

  let quad_offset = kLocalOffsets[input.vertexID] * scale;

  /* Create a 'billboard' quad from the particle position. */
  // Important: in Slang, matrices accessors are always row major (different than)
  let quad_right = normalize(float3(view[0][0], view[0][1], view[0][2]));
  let quad_up    = normalize(float3(view[1][0], view[1][1], view[1][2]));

  let vertex_offset = quad_offset.x * quad_right
                    + quad_offset.y * quad_up
                    ;

  let worldPos = mul(world, float4(center, 1.0)).xyz
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
  let dLimit = 207.0;
  let dRange = 5.0;

  let d = dot(input.positionWS, input.positionWS);
  let borderAlpha = 1.0 - smoothstep(dLimit - dRange, dLimit + dRange, d);

  let pt = 2.0f * (input.texCoord - 0.5f);
  let l = 1.0f - abs(dot(pt, pt));

  let alpha = borderAlpha * smoothstep(0.65f, 0.85f, l);

  var color = float4(float3(l), alpha);

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
