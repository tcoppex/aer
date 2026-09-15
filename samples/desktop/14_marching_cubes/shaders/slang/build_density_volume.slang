#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

[[vk::binding(kDescriptorSetBinding_DensityTexture_Storage, 0)]]
RWTexture3D<float4> outDensityVolume;

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

  let chunkAttributes = pushConstant.chunkAttributes.xyz;

  let ext_coords    = float3(coords) * float3(kChunkDim * kInvChunkDimMinusOne);

  let voxelCoords   = (ext_coords - float3(kChunkMargin)) * kInvChunkDim;
  let voxelCoordsWS = fma(voxelCoords, float3(kChunkSize), chunkAttributes);

  var density: float = 0.0;
  // density = computeDensity(voxelCoordsWS);

  outDensityVolume[coords] = float4(density, 0.0, 0.0, 0.0);
}

// ----------------------------------------------------------------------------
