#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

[[vk::binding(kDescriptorSetBinding_DensityTexture_Storage, 0)]]
RWTexture3D<float4> outDensityVolume;

// ----------------------------------------------------------------------------

float computeDensity(float3 ws)
{
  let p = ws;
  let t = float2(16.5f, 2.0);
  let q = float2(length(p.xz) - t.x, p.y);
  return length(q) - t.y;
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
}

// ----------------------------------------------------------------------------
