#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
  let kTargetGroupSize = kCompute_MaxLinearGroupSize; //

  let g_totalCount  = &((uint32_t*)pushConstant.atomicCountBuffer)[pushConstant.atomicCountIndex];
  let out_indirect  = ((uint3*)pushConstant.indirectBuffer);

  if (pushConstant.atomicCountLimit > 0) {
    *g_totalCount   = *g_totalCount % pushConstant.atomicCountLimit;
  }

  out_indirect->x = uint32_t((*g_totalCount + (kTargetGroupSize - 1u)) / kTargetGroupSize);
  out_indirect->y = 1u;
  out_indirect->z = 1u;
}

// ----------------------------------------------------------------------------
