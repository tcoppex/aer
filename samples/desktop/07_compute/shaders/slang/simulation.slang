// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorBinding_UBO_Data, 0)]]
ConstantBuffer<UniformData> uData;

// ---------------

// [[vk::binding(kDescriptorBinding_SBO_Positions, 0)]]
// StructuredBuffer<float4> r_Positions;

// [[vk::binding(kDescriptorBinding_SBO_DotProducts, 0)]]
// StructuredBuffer<float> r_DotProducts;

[[vk::binding(kDescriptorBinding_SBO_Positions, 0)]]
RWStructuredBuffer<float4> rw_Positions;

[[vk::binding(kDescriptorBinding_SBO_Indices, 0)]]
RWStructuredBuffer<uint> rw_Indices;

[[vk::binding(kDescriptorBinding_SBO_DotProducts, 0)]]
RWStructuredBuffer<float> rw_DotProducts;

// ---------------

struct PushConstant_ {
  [[vk::offset(64)]]
  PushConstant_Compute compute;
};

[[vk::push_constant]]
PushConstant_ pushConstant;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_Simulation_kernelSize_x)]
void compute_Simulation(
  uint gid : SV_DispatchThreadID
) {
  if (gid >= pushConstant.compute.numElems) {
    return;
  }

  // We fetch the original particle position on the back buffer, as we will
  // update their xz position too (not just their height).
  const float4 particle = rw_Positions[pushConstant.compute.numElems + gid];
  float3 pos = particle.xyz;

  // --------------------------------

  float W = 2.50f;
  float A = 0.20f;
  float S = 2.10f * pushConstant.compute.time;

  pos = wave(pos, float2(1.0f, +0.0f), W, A, S)
      + wave(pos, float2(1.0f, -0.2f), W * 0.250f, A * 0.50f, S * 1.10f)
      + wave(pos, float2(0.8f, +0.5f), W * 0.125f, A * 0.25f, S * 1.21f)
      + wave(pos, float2(0.8f, +3.1f), W * 0.500f, A * 0.75f, S * 0.90f)
      ;
  pos.xz = particle.xz + pos.xz / 4.0f;

  float size = 5.0f * (1.0f - smoothstep(-0.65f, +0.65f, pos.y));

  // --------------------------------

  rw_Positions[gid] = float4(pos, size);
}

float3 wave(
  in float3 pt,
  in float2 direction,
  in float wavelength,
  in float amplitude,
  in float speed
) {
  direction = normalize(direction);

  const float w = 2.0f / wavelength;
  const float param = dot(pt.xz, direction) * w + speed;
  const float2 crest = direction * cos(param) * (1.0f / w);

  return float3(
    crest.x,
    amplitude * sin(param),
    crest.y
  );
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_FillIndex_kernelSize_x)]
void compute_FillIndices(
  uint gid : SV_DispatchThreadID
) {
  if (gid >= pushConstant.compute.numElems) {
    return;
  }

  rw_Indices[gid] = gid;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_DotProduct_kernelSize_x)]
void compute_DotProduct(
  uint gid : SV_DispatchThreadID
) {
  if (gid >= pushConstant.compute.numElems) {
    return;
  }

  // The default camera front in view space.
  float3 kTargetVS = float3(0.0f, 0.0f, -1.0f);

  // Transform a particle's position to view space.
  float4x4 modelView = mul(
    uData.scene.camera.viewMatrix,
    pushConstant.compute.model.worldMatrix
  );
  float4 positionVS = mul(modelView, float4(rw_Positions[gid].xyz, 1.0f));

  // Distance of the particle from the camera.
  rw_DotProducts[gid] = dot(kTargetVS, positionVS.xyz);
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_SortIndex_kernelSize_x)]
void compute_SortIndices(
  uint gid : SV_DispatchThreadID
) {
  if (gid >= pushConstant.compute.numElems) {
    return;
  }

  const uint read_offset      = pushConstant.compute.readOffset;
  const uint write_offset     = pushConstant.compute.writeOffset;
  const uint block_width      = pushConstant.compute.blockWidth;
  const uint max_block_width  = pushConstant.compute.maxBlockWidth;

  const uint pair_distance = block_width / 2u;

  const uint block_offset = (gid / pair_distance) * block_width;
  const uint left_id      = block_offset + (gid % pair_distance);
  const uint right_id     = left_id + pair_distance;

  uint left_data  = rw_Indices[read_offset + left_id];
  uint right_data = rw_Indices[read_offset + right_id];

  const bool order = bool((left_id / max_block_width) & 1u);
  CompareAndSwap(rw_DotProducts, order, left_data, right_data);

  rw_Indices[write_offset + left_id]  = left_data;
  rw_Indices[write_offset + right_id] = right_data;
}

void CompareAndSwap(
  in RWStructuredBuffer<float> Keys, // [only needs read-only]
  in bool is_greater_than,
  inout uint left,
  inout uint right
) {
  if (is_greater_than == (Keys[left] > Keys[right])) {
    left  ^= right;
    right ^= left;
    left  ^= right;
  }
}

// ----------------------------------------------------------------------------
