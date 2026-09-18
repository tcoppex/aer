#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

[[vk::binding(kDescriptorSetBinding_DensityTexture_Storage, 0)]]
RWTexture3D<float4> outDensityVolume;

// ----------------------------------------------------------------------------

float3x3 rotateAxisAngle(float3 _axis, float angle)
{
  float3 axis = normalize(_axis);
  float c = cos(angle);
  float s = sin(angle);
  float t = 1.0 - c;

  return float3x3(
    t * axis.x * axis.x + c,       t * axis.x * axis.y - s * axis.z,  t * axis.x * axis.z + s * axis.y,
    t * axis.x * axis.y + s * axis.z,  t * axis.y * axis.y + c,       t * axis.y * axis.z - s * axis.x,
    t * axis.x * axis.z - s * axis.y,  t * axis.y * axis.z + s * axis.x,  t * axis.z * axis.z + c
  );
}

// ----------------------------------------------------------------------------

float opUnion(float a, float b) {
  return min(a, b);
}

float opIntersection(float a, float b) {
  return max(a, b);
}

float opSubstraction(float a, float b) {
  return max(a, -b);
}

float opSmoothUnion(float a, float b, float k) {
  float res = exp(-k * a) + exp(-k * b);
  return -log(res) / k;
}

float3 opRepeat(float3 p, float3 c) {
  return p - c * (floor(c / p) + 0.5);
}

float opDisplacement(float3 p, float3 d) {
  float3 dp = d * p;
  return sin(p.x) * sin(p.y) * sin(p.z);
}

float sdPlane(float3 p, float4 n) {
  return n.w + dot(p, n.xyz);
}

float sdSphere(float3 p, float r) {
  return length(p) - r;
}

float udRoundBox(float3 p, float3 b, float r) {
  return length(max(abs(p) - b, float3(0.0))) - r;
}

float sdCylinder(float3 p, float r) {
  return length(p.xy) - r;
}

float sdCylinder2(float3 p, float3 c) {
  return opIntersection(length(p.xz - c.xy) - c.z, abs(p.y) - c.y);
}

float sdTorus(float3 p, float2 t) {
  float2 q = float2(length(p.xz) - t.x, p.y);
  return length(q) - t.y;
}

// ----------------------------------------------------------------------------

float computeDensity(float3 ws)
{
  var d = 0.0;
  var p = ws;

  let radius = 8.0;

  let axis = float3(1, 0, 1);
  p = mul(rotateAxisAngle(axis, 3.14/4.0), p);

  d = udRoundBox(p, float3(radius), 0.175*radius);

  let hs = 0.34;
  let helix = float2(cos(hs * ws.y), sin(hs * ws.y));
  d += opIntersection(dot(helix, hs * ws.xz), d);

  p = ws.yzx;
  p = mul(rotateAxisAngle(float3(1), 0.472), p);

  d = opSmoothUnion(
    d,
    sdTorus(p, float2(2.1 * radius, 2.0)),
    0.45
  );

  return d;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(
  kCompute_BuildDensity_kernelSize,
  kCompute_BuildDensity_kernelSize,
  kCompute_BuildDensity_kernelSize
)]
void main(uint3 coords : SV_DispatchThreadID)
{
  if (any(coords >= pushConstant.gridSize)) {
    return;
  }

  let ext_coords    = float3(coords) * float3(kChunkDim * kInvChunkDimMinusOne);
  let voxelCoords   = (ext_coords - float3(kChunkMargin)) * kInvChunkDim;
  let voxelCoordsWS = fma(voxelCoords, float3(kChunkSize), pushConstant.chunkAttributes.xyz);

  let density = computeDensity(voxelCoordsWS);

  outDensityVolume[coords] = float4(density, 0.0, 0.0, 0.0);

  // [reset atomic count buffer]
  if (all(coords == uint3(0))) {
    let g_atomicCount = (uint4*)pushConstant.atomicCountBuffer;
    *g_atomicCount = uint4(0);

    let g_indexCount = ((uint32_t*)pushConstant.drawIndexedIndirectBuffer);
    *g_indexCount = 0u;
  }
}

// ----------------------------------------------------------------------------
