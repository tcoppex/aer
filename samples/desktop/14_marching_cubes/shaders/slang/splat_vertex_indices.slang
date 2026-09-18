#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSetBinding_VertexIndicesVolume, 0)]]
RWTexture3D<uint32_t> outVertexIndicesVolume;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_MaxLinearGroupSize, 1, 1)]
void main(
  uint gid : SV_DispatchThreadID
)
{
  let kVertexCount = ((uint32_t*)pushConstant.atomicCountBuffer)[ATOMIC_COUNT_VERT]; //

  if (gid >= kVertexCount) {
    return;
  }

  let g_inVerticesToGenerate = ((uint32_t*)pushConstant.verticesToGenerateBuffer);
  let packedData = g_inVerticesToGenerate[gid];

  let coords_xyz = uint3(
    (packedData >>  0) & 0xFFU,
    (packedData >>  8) & 0xFFU,
    (packedData >> 16) & 0xFFU
  );
  let cube_case = (packedData >> 24) & 0xFFU;
  let edge = cube_case & 0xFU;

  let axis_offset = (edge == 0u) ? 1u : ((edge == 8u) ? 2u : 0u);

  let coords = uint3(
    3u * coords_xyz.x + axis_offset,
    coords_xyz.y,
    coords_xyz.z
  );
  outVertexIndicesVolume[coords] = gid;
}

// ----------------------------------------------------------------------------
