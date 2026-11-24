// ----------------------------------------------------------------------------
// Debug draw: world space segments expanded to screen space quads.
// One instance = one segment, drawn as a 4 vertices triangle strip:
//   (a,-) (a,+) (b,-) (b,+)
// A zero length segment is a square point (square caps).
// ----------------------------------------------------------------------------

struct PushConstant_t {
  float4x4 viewProjection;
  float2   viewport;      // pixels
  float2   padding;
};

[[vk::push_constant]]
PushConstant_t pc;

struct VSInput {
  [[vk::location(0)]]
  float3 a       : POSITION0;

  [[vk::location(1)]]
  float  width   : TEXCOORD0;   // pixels

  [[vk::location(2)]]
  float3 b       : POSITION1;

  [[vk::location(3)]]
  float4 colorA  : COLOR0;

  [[vk::location(4)]]
  float4 colorB  : COLOR1;
};

struct VSOutput {
  float4 position : SV_Position;

  [[vk::location(0)]]
  float4 color    : COLOR;
};

static const float kMinW = 1.0e-3;

/* -------------------------------------------------------------------------- */

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint vertexId : SV_VertexID) {
  uint  endpoint = vertexId >> 1u;
  float side     = ((vertexId & 1u) == 0u) ? -1.0 : 1.0;

  VSOutput output;
  output.color = (endpoint == 0u) ? input.colorA : input.colorB;

  float4 ca = mul(pc.viewProjection, float4(input.a, 1.0));
  float4 cb = mul(pc.viewProjection, float4(input.b, 1.0));

  // Entirely behind the camera: degenerate quad (outside the clip volume).
  if (ca.w < kMinW && cb.w < kMinW) {
    output.position = float4(0.0, 0.0, 2.0, 1.0);
    return output;
  }

  // Clip the segment to the w > kMinW half space.
  if (ca.w < kMinW) {
    ca = lerp(ca, cb, (kMinW - ca.w) / (cb.w - ca.w));
  } else if (cb.w < kMinW) {
    cb = lerp(cb, ca, (kMinW - cb.w) / (ca.w - cb.w));
  }

  float4 c = (endpoint == 0u) ? ca : cb;

  // Direction of the segment on screen (pixels).
  float2 delta = (cb.xy / cb.w - ca.xy / ca.w) * 0.5 * pc.viewport;
  float  len   = length(delta);
  float2 dir   = (len > 1.0e-4) ? (delta / len) : float2(1.0, 0.0);
  float2 normal = float2(-dir.y, dir.x);

  // Side offset + square cap along the segment.
  float  halfWidth = 0.5 * input.width;
  float2 offsetPx  = normal * (side * halfWidth)
                   + dir * ((endpoint == 0u) ? -halfWidth : halfWidth);

  float2 ndc = c.xy / c.w + offsetPx * 2.0 / pc.viewport;
  output.position = float4(ndc * c.w, c.z, c.w);
  return output;
}

// ----------------------------------------------------------------------------

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
  return input.color;
}

/* -------------------------------------------------------------------------- */
