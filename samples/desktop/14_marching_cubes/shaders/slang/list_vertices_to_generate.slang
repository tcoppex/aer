#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_MaxLinearGroupSize, 1, 1)]
void main(
  uint gid : SV_DispatchThreadID
)
{
  let g_cellCount = ((uint32_t*)pushConstant.cellCountBuffer);

  if (gid >= *g_cellCount) {
    return;
  }

  let g_nonEmptyCells = ((uint32_t*)pushConstant.nonEmptyCellsBuffer);
  let g_outVertices   = ((uint32_t*)pushConstant.verticesToGenerateBuffer);
  let g_atomicCount   = ((uint32_t*)pushConstant.atomicCountBuffer);

  let packedData = g_nonEmptyCells[gid];
  let coords = uint3(
    packedData & 0xFFU,
    (packedData >> 8) & 0xFFU,
    (packedData >> 16) & 0xFFU
  );
  let cube_case = (packedData >> 24) & 0xFFU;

  let bits = uint4(
    (cube_case >> 3) & 1U,
    (cube_case >> 1) & 1U,
    (cube_case >> 4) & 1U,
    cube_case & 1U
  );

  let vertOnEdges = bool3(bits.xyz != bits.www);

  let numVerts = (vertOnEdges.x ? 1U : 0U)
               + (vertOnEdges.y ? 1U : 0U)
               + (vertOnEdges.z ? 1U : 0U);

  uint out_index;
  InterlockedAdd(*g_atomicCount, numVerts, out_index);

  let edge_nums = uint3(3U, 0U, 8U);

  [unroll]
  for (int i = 0; i < 3; i++) {
    if (vertOnEdges[i]) {
      g_outVertices[out_index] = ((coords.x & 0xFFU) << 0)
                               | ((coords.y & 0xFFU) << 8)
                               | ((coords.z & 0xFFU) << 16)
                               | ((edge_nums[i] & 0xFFU) << 24)
                               ;
      out_index++;
    }
  }
}

// ----------------------------------------------------------------------------
