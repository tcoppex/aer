// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::constant_id(0)]]
const bool sc_faceCamera = false;

[[vk::binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[[vk::binding(kDescriptorSetBinding_StorageBuffer_Position, 0)]]
StructuredBuffer<float4> Positions;

[[vk::binding(kDescriptorSetBinding_StorageBuffer_Index, 0)]]
StructuredBuffer<uint> Indices;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSInput {
  uint instanceID   : SV_InstanceID;
  uint vertexID     : SV_VertexID;
};

struct VSOutput {
  float4 position   : SV_Position;
  float2 texCoord   : TEXCOORD0;
};

// ----------------------------------------------------------------------------

static const float kTwoPi = 6.28318530718;

static const float2 kSpriteSize = float2(0.035);

static const float2 kLocalOffsets[6] = {
  float2(-0.5, -0.5),
  float2( 0.5, -0.5),
  float2(-0.5,  0.5),

  float2(-0.5,  0.5),
  float2( 0.5, -0.5),
  float2( 0.5,  0.5)
};

static const float2 kLocalTexCoords[6] = {
  float2(0.0, 0.0),
  float2(1.0, 0.0),
  float2(0.0, 1.0),

  float2(0.0, 1.0),
  float2(1.0, 0.0),
  float2(1.0, 1.0)
};

[shader("vertex")]
VSOutput vertexMain(VSInput input) {
  let projection : float4x4 = uData.scene.camera.projectionMatrix;
  let view       : float4x4 = uData.scene.camera.viewMatrix;
  let world      : float4x4 = pushConstant.model.worldMatrix;
  let viewProj   : float4x4 = mul(projection, view);

  // --------------------

  let quad_offset : float2 = kLocalOffsets[input.vertexID] * kSpriteSize;
  var center : float3 = Positions[Indices[input.instanceID]].xyz;

  /* Simulate particles. */
  ///
  /// One pass particle simulation on the vertex shader is possible when we don't
  /// need to process them between frame (eg. sorting them for alpha blending or
  /// to manage their lifecycle).
  ///
  /// It's perfect to render per-vertex attributes, like normals.
  ///

  /* Move Points on a sphere. */
  let phi = center.x * kTwoPi * (1.0f - 1.0f/512.0f);
  let theta = center.z * kTwoPi * 0.5;
  let ct = cos(theta);

  let normal = float3(
    ct * cos(phi),
    sin(theta),
    ct * sin(phi)
  );
  center += normal;

  /* Move along the normal */
  let factor = 0.2f * sin(8.0f * dot(center, center) + 0.75f * pushConstant.time);
  center = factor * normal;

  /* Make a Billboard quad out of the particle position. */
  float3 quad_right, quad_up;

  if (sc_faceCamera)
  {
    /* Face the camera. */
    quad_right = normalize(float3(view[0].x, view[1].x, view[2].x));
    quad_up    = normalize(float3(view[0].y, view[1].y, view[2].y));
  }
  else
  {
    /* Face a forward vector (need back culling off). */
    let quad_front : float3 = mul(world, float4(-normal, 1.0f)).xyz;
    quad_right = normalize(cross(quad_front, float3(0.0, 1.0, 0.0)));
    quad_up    = normalize(cross(quad_right, quad_front));
  }

  float3 vertex_offset = quad_offset.x * quad_right
                       + quad_offset.y * quad_up
                       ;

  float3 worldPos = mul(world, float4(center, 1.0f)).xyz
                  + vertex_offset
                  ;

  // --------------------

  VSOutput vertex;
  vertex.position = mul(viewProj, float4(worldPos, 1.0));
  vertex.texCoord = kLocalTexCoords[input.vertexID];

  return vertex;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  float2 pt = 2.0f * (input.texCoord - 0.5f);

  float d = 1.0f - abs(dot(pt, pt));

  float alpha = smoothstep(0.0f, 0.3f, d)
              - smoothstep(0.3f, 0.7f, d)
              ;

  float4 color = float4(1.0, 0.75, 0.25, 0.075 * alpha);
  color *= d;
  color.xy *= 1.5 * input.texCoord;

  return color;
}

// ----------------------------------------------------------------------------
