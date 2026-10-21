#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  let indexCount    = ((uint32_t*)pushConstant.atomicCountBuffer)[ATOMIC_COUNT_INDEX];
  let g_indexCount  = ((uint32_t*)pushConstant.drawIndexedIndirectBuffer);

  *g_indexCount = min(indexCount, kChunkMaxIndices);
}

// ----------------------------------------------------------------------------
