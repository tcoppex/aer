#include "../interop.h"

#include "custom_density.slang.h" //

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

[[vk::binding(kDescriptorSetBinding_DensityTexture_Storage, 0)]]
[[vk::image_format("r16f")]]
RWTexture3D<float> outDensityVolume;

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

  // (chunkAttributes.w holds the chunk size in world space)
  let voxelCoordsWS = fma(voxelCoords, float3(pushConstant.chunkAttributes.w), pushConstant.chunkAttributes.xyz);

  outDensityVolume[coords] = computeDensity(voxelCoordsWS);

  // [reset atomic count buffer]
  if (all(coords == uint3(0))) {
    let g_atomicCount = (uint4*)pushConstant.atomicCountBuffer;
    *g_atomicCount = uint4(0);

    let g_indexCount = ((uint32_t*)pushConstant.drawIndexedIndirectBuffer);
    *g_indexCount = 0u;
  }
}

// ----------------------------------------------------------------------------
