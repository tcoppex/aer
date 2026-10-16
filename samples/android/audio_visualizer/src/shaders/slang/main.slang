
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

// ----------------------------------------------------------------------------

struct VSOutput
{
  float4 position : SV_Position;
  float2 uv   : TEXCOORD0;
  nointerpolation uint viewIndex : TEXCOORD1;
};

// ----------------------------------------------------------------------------
// Vertex Shader
// ----------------------------------------------------------------------------

[shader("vertex")]
VSOutput vertexMain(
  uint vertexID : SV_VertexID,
  uint viewIndex : SV_ViewID)
{
  float2 positions[3] = {
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0)
  };

  float2 uvs[3] = {
    float2(0.0,  1.0),
    float2(2.0,  1.0),
    float2(0.0, -1.0)
  };

  VSOutput output;
  output.position = float4(positions[vertexID], 0.0, 1.0);
  output.uv = uvs[vertexID];
  output.viewIndex = viewIndex;

  return output;
}

// ----------------------------------------------------------------------------
// Fragment Shader
// ----------------------------------------------------------------------------

float2 rotate2D(float2 v, float a) {
  let s = sin(a);
  let c = cos(a);
  return float2(c * v.x - s * v.y, s * v.x + c * v.y);
}

float4 audioVisualEffect(
  float2 p,
  float time,
  float bass,
  float mids,
  float highs
) {
  float3 ro = float3(0.0, 0.0, time * 2.5);
  float3 rd = normalize(float3(p, 1.0 - dot(p, p) * 0.2));

  rd.xy = rotate2D(rd.xy, sin(time * 0.5) * 0.2 + highs * 0.83);

  let pi      = 3.14159f;
  let slices  = 6.0f;
  let kseg    = pi / slices;

  let maxSteps = 30;

  float accGlow = 0.0f;
  float t = 0.0f;

  for (int i = 0; i < maxSteps; i++) {
    float3 pos = ro + rd * t;

    float3 q = pos;
    q.z = fmod(pos.z, 4.0) - 2.0;

    float a = atan2(q.y, q.x);
    float r = length(q.xy);

    a = abs(fmod(a + pi, 2.0 * kseg) - kseg);
    q.xy = float2(cos(a + a *   mids), sin(a + a * bass)) * r;

    let bassPulse = bass * 0.9 + sin(pos.z * 1.5 + time * 3.0) * 0.25 * mids;
    float d = r - (1.2 + bassPulse);
    d += sin(q.x * 6.0 + time * 4.0) * cos(q.y * 6.0 - time * 2.0) * (0.05 + highs * 0.1);

    accGlow += (0.0004 + highs * 0.0001) / (0.01 + abs(d) * abs(d));
    t += max(abs(d) * 0.6, 0.03);

    if (t > 15.0) break;
  }

  let colA = float3(0.1, 0.4, 1.0);
  let colB = float3(1.0, 0.1, 0.5);
  let colC = float3(0.6, 1.0, 0.1);
  let colorMix = sin(time * 0.2 + ro.z * 0.1) * 0.5 + 0.5;
  float3 baseColor = lerp(colA, colB, colorMix);
  baseColor = lerp(baseColor, colC, highs);

  float3 color = baseColor * accGlow * (0.15 + bass * 0.85);
  color *= (1.0 - dot(p, p) * 0.8);
  color += highs * float3(0.1, 0.1, 0.2) * (sin(p.xyx * 15.0 + (1.0 - bass) * time * 4.0));

  let alpha = 1.2*length(color);

  return saturate(float4(color, alpha));
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0
{
  let eye       = input.viewIndex;
  let camera    = uBO.uCameras[eye];
  let parallax  = 0.12f;

  var uv = input.uv;
  uv.x += (eye == 0) ? -parallax : +parallax;

  let p = uv - float2(0.5, 0.5);

  let time  = pushConstant.time;
  let bass  = saturate(pushConstant.audioAmplitude);
  let mids  = saturate(pushConstant.audioMids);
  let highs = saturate(pushConstant.audioHighs);

  return audioVisualEffect(p, time, bass, mids, highs);
}