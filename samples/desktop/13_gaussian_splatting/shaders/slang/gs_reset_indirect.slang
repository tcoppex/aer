#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[ForceInline]
uint32_t groupCount(uint32_t numElems, uint32_t threadsPerGroup) {
  return (numElems + threadsPerGroup - 1) / threadsPerGroup;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  let g_indirect = (uint32_t*)pushConstant.scan_total_count_indirect_addr;

  let lastId     = pushConstant.numElems - 1;
  let lastCount  = ((uint32_t*)pushConstant.scan_input_addr)[lastId];
  let lastOffset = ((uint32_t*)pushConstant.scan_output_addr)[lastId];

  let totalCount = (lastOffset + lastCount) % pushConstant.maxKeyValueCapacity;

  // Radix histogram grid dimension.
  g_indirect[0] = groupCount(totalCount, pushConstant.tileSize);
  g_indirect[1] = 1;
  g_indirect[2] = 1;

  // Gaussian Splat total Key-Value count.
  g_indirect[3] = totalCount;

  // Radix digit binning grid dimension.
  g_indirect[4] = groupCount(totalCount, pushConstant.radixSize);
  g_indirect[5] = 1;
  g_indirect[6] = 1;
}

// ----------------------------------------------------------------------------
