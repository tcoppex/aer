#include "../interop.h"

// ----------------------------------------------------------------------------

static const float kInvWindowDim          = 1.0 / (kWindowDim);
static const float kInvWindowDimMinusOne  = 1.0 / (kWindowDim - 1);
static const float kVoxelSizeWS           = (kChunkSize / float(kChunkDim - 1.0));

// ----------------------------------------------------------------------------

static const float3 kEdgeStart[12] = {
  float3(0.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 0.0, 0.0),
  float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 1.0), float3(1.0, 0.0, 1.0), float3(0.0, 0.0, 1.0),
  float3(0.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(1.0, 1.0, 0.0), float3(1.0, 0.0, 0.0),
};

static const float3 kEdgeEnd[12] = {
  float3(0.0, 1.0, 0.0), float3(1.0, 1.0, 0.0), float3(1.0, 1.0, 0.0), float3(1.0, 0.0, 0.0),
  float3(0.0, 1.0, 1.0), float3(1.0, 1.0, 1.0), float3(1.0, 1.0, 1.0), float3(1.0, 0.0, 1.0),
  float3(0.0, 0.0, 1.0), float3(0.0, 1.0, 1.0), float3(1.0, 1.0, 1.0), float3(1.0, 0.0, 1.0),
};

static const float3 kEdgeDir[12] = {
  float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0),
  float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0), float3(0.0, 1.0, 0.0), float3(1.0, 0.0, 0.0),
  float3(0.0, 0.0, 1.0), float3(0.0, 0.0, 1.0), float3(0.0, 0.0, 1.0), float3(0.0, 0.0, 1.0),
};

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSetBinding_SamplerNearest, 0)]]
SamplerState uSamplerNearest : register(s0);

[[vk::binding(kDescriptorSetBinding_SamplerLinear, 0)]]
SamplerState uSamplerLinear : register(s1);

[[vk::binding(kDescriptorSetBinding_DensityTexture_Sampling, 0)]]
Texture3D<float> uDensityTexture : register(t0); //


float sampleNearest(float3 uvw) {
  return uDensityTexture.SampleLevel(uSamplerNearest, uvw, 0.0f).r;
}

float sampleLinear(float3 uvw) {
  return uDensityTexture.SampleLevel(uSamplerLinear, uvw, 0.0f).r;
}

float sampleGradient(float3 uvw, float3 offset) {
  return sampleLinear(uvw + offset) - sampleLinear(uvw - offset);
}

float3 sampleNormal(float3 uvw) {
  let offset = float2(kTexelSize, 0.0f);
  let n = float3(
    sampleGradient(uvw, offset.xyy),
    sampleGradient(uvw, offset.yxy),
    sampleGradient(uvw, offset.yyx)
  );
  return normalize(n);
}

// ----------------------------------------------------------------------------

// U(i) = pow(vec4f(1-i/16), 0.3)
static const float kAOWeights[16] = {
  1.000000000f,
  0.980824675f,
  0.960732353f,
  0.939608660f,
  0.917314755f,
  0.893679531f,
  0.868488366f,
  0.841466359f,
  0.812252396f,
  0.780357156f,
  0.745091108f,
  0.705431757f,
  0.659753955f,
  0.605202038f,
  0.535886731f,
  0.435275282f
};

static const float3 kSphereRays64[64] = {
  float3(-0.176084807, 0.000000000, 0.984375000),
  float3(0.000000000, 0.302576824, 0.953125000),
  float3(-0.000000000, -0.387487399, 0.921875000),
  float3(0.321548682, 0.321548682, 0.890625000),
  float3(-0.361576140, -0.361576140, 0.859375000),
  float3(-0.396364090, 0.396364090, 0.828125000),
  float3(0.427194472, -0.427194472, 0.796875000),
  float3(0.594319833, 0.246175335, 0.765625000),
  float3(-0.627077650, -0.259744067, 0.734375000),
  float3(-0.272113279, 0.656939569, 0.703125000),
  float3(0.283440054, -0.684284824, 0.671875000),
  float3(0.293844965, 0.709404501, 0.640625000),
  float3(-0.303422864, -0.732527593, 0.609375000),
  float3(-0.753837852, 0.312249862, 0.578125000),
  float3(0.773485129, -0.320388031, 0.546875000),
  float3(0.840350919, 0.167156191, 0.515625000),
  float3(-0.858050281, -0.170676813, 0.484375000),
  float3(-0.173912680, 0.874318085, 0.453125000),
  float3(0.176879422, -0.889232902, 0.421875000),
  float3(0.511430120, 0.765409264, 0.390625000),
  float3(-0.518454382, -0.775921815, 0.359375000),
  float3(-0.785434726, 0.524810705, 0.328125000),
  float3(0.793983927, -0.530523099, 0.296875000),
  float3(0.801600254, 0.535612166, 0.265625000),
  float3(-0.808310078, -0.540095527, 0.234375000),
  float3(-0.543988157, 0.814135811, 0.203125000),
  float3(0.547302663, -0.819096319, 0.171875000),
  float3(0.193151696, 0.971039150, 0.140625000),
  float3(-0.193919889, -0.974901116, 0.109375000),
  float3(-0.977787580, 0.194494042, 0.078125000),
  float3(0.979707165, -0.194875872, 0.046875000),
  float3(0.995063237, 0.098005175, 0.015625000),
  float3(-0.995063237, -0.098005175, -0.015625000),
  float3(-0.097909396, 0.994090783, -0.046875000),
  float3(0.097717558, -0.992143016, -0.078125000),
  float3(0.630587278, 0.768372822, -0.109375000),
  float3(-0.628089275, -0.765328996, -0.140625000),
  float3(-0.761507104, 0.624952730, -0.171875000),
  float3(0.756895361, -0.621167970, -0.203125000),
  float3(0.857356463, 0.458266577, -0.234375000),
  float3(-0.850239502, -0.454462482, -0.265625000),
  float3(-0.450144451, 0.842161034, -0.296875000),
  float3(0.445297532, -0.833093087, -0.328125000),
  float3(0.270891696, 0.893010245, -0.359375000),
  float3(-0.267221529, -0.880911326, -0.390625000),
  float3(-0.867613788, 0.263187765, -0.421875000),
  float3(0.853061581, -0.258773402, -0.453125000),
  float3(0.837189281, 0.253958592, -0.484375000),
  float3(-0.819920228, -0.248720082, -0.515625000),
  float3(-0.243030474, 0.801164105, -0.546875000),
  float3(0.236857263, -0.780813756, -0.578125000),
  float3(0.373762060, 0.699259631, -0.609375000),
  float3(-0.361963822, -0.677186681, -0.640625000),
  float3(-0.653207822, 0.349146855, -0.671875000),
  float3(0.627104460, -0.335194317, -0.703125000),
  float3(0.524676174, 0.430590608, -0.734375000),
  float3(-0.497267693, -0.408097049, -0.765625000),
  float3(-0.383265034, 0.467009795, -0.796875000),
  float3(0.355605014, -0.433305964, -0.828125000),
  float3(0.050120661, 0.508883610, -0.859375000),
  float3(-0.044572168, -0.452548816, -0.890625000),
  float3(-0.385621541, 0.037980407, -0.921875000),
  float3(0.301119834, -0.029657715, -0.953125000),
  float3(0.175872705, 0.008640072, -0.984375000),
};

float calculateAO(float3 uvw, float3 n) {
  var ao = 1.0f;

  let kAORayCount = 64u;
  let kAOStepCount = 16u;
  let kRayStartOffset = 1.75f;

  // [unroll]
  for (uint i = 0u; i < kAORayCount; i++) {
    let ray_dir = kSphereRays64[i];
    let ray_start = uvw;
    let ray_step = ray_dir * (kTexelSize / (float)kAOStepCount);

    var ray_ao = 1.0f;
    var ray_curr = mad(float3(kRayStartOffset * kTexelSize), ray_dir, ray_start);

    [unroll]
    for (uint j = 0u; j < kAOStepCount; j++) {
      ray_curr += ray_step;
      let density = sampleLinear(ray_curr);
      let coeff = (4.5f * density) * kAOWeights[j];
      ray_ao = smoothstep(0.0f, ray_ao, coeff);
    }

    ao += (1.0f - smoothstep(pow(ray_ao, 400.0f), 1.0f, dot(ray_dir, n)));
  }
  ao = saturate(ao / (float)kAORayCount);

  return ao;
}

// ----------------------------------------------------------------------------

struct Vertex {
  float3 position;
  float ao;
  float3 normal;
};

Vertex place_vertex_on_edge(float3 coordsWS, float3 texcoord, uint edge_id) {
  Vertex v;

  let edge_start = kEdgeStart[edge_id];
  let edge_end = kEdgeEnd[edge_id];
  let edge_dir = kEdgeDir[edge_id];
  let scale = float3(kInvWindowDimMinusOne);

  let p1 = sampleNearest(mad(edge_start, scale, texcoord));
  let p2 = sampleNearest(mad(edge_end, scale, texcoord));
  let t = float3(saturate(p1 / (p1 - p2)));
  let pos = mad(edge_dir, t, edge_start);
  let uvw = mad(pos, scale, texcoord);

  v.position = mad(float3(kVoxelSizeWS), pos, coordsWS);
  v.normal = sampleNormal(uvw);
  v.ao = calculateAO(uvw, v.normal);

  return v;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_MaxLinearGroupSize, 1, 1)]
void main(
  uint gid : SV_DispatchThreadID
) {
  let kVertexCount = ((uint32_t*)pushConstant.atomicCountBuffer)[ATOMIC_COUNT_VERT];

  if (gid >= kVertexCount) {
    return;
  }

  let g_inVerticesToGenerate  = ((uint32_t*)pushConstant.verticesToGenerateBuffer);
  let g_outVertices           = ((float4*)pushConstant.verticesBuffer);

  let packedData = g_inVerticesToGenerate[gid];
  let coords = float3(
    (float)(packedData & 0xFFU),
    (float)((packedData >> 8) & 0xFFU),
    (float)((packedData >> 16) & 0xFFU)
  );
  let edge = (packedData >> 24) & 0xFFU;

  let coordsWS = mad(float3(kVoxelSizeWS), coords, pushConstant.chunkAttributes.xyz);
  let texcoord = (coords + float3(kChunkMargin)) * kInvWindowDim;

  let v = place_vertex_on_edge(coordsWS, texcoord, edge);

  let out_index = 2u * gid;

  g_outVertices[out_index + 0u] = float4(v.position, v.ao);
  g_outVertices[out_index + 1u] = float4(v.normal, 0.0f);
}

// ----------------------------------------------------------------------------