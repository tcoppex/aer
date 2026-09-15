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

  let totalCount = *((uint32_t*)pushConstant.atomicCountBuffer);
  let out_indirect = ((uint3*)pushConstant.indirectBuffer);

  out_indirect->x = uint32_t((totalCount + (kTargetGroupSize - 1u)) / kTargetGroupSize);
  out_indirect->y = 1u;
  out_indirect->z = 1u;
}

// ----------------------------------------------------------------------------
