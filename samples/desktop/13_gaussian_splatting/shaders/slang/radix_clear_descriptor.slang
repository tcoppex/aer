// ----------------------------------------------------------------------------

#include "../radix_interop.h"

#define NUM_THREADS   kRadixSize
#define NUM_FLAGS     3

// ----------------------------------------------------------------------------

[[vk::push_constant]]
RadixPushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(NUM_THREADS, 1, 1)]
void main(
  uint3 groupID : SV_GroupID,
  uint tid : SV_GroupIndex
) {
  let g_descriptor  = (uint32_t*)pushConstant.descriptor_addr;
  let g_counter     = (uint32_t*)pushConstant.counter_addr;

  let tileID = groupID.x;
  let index  = (tileID * NUM_THREADS + tid) * NUM_FLAGS;

  g_descriptor[index + 0] = 0;
  g_descriptor[index + 1] = 0;
  g_descriptor[index + 2] = 0;

  if ((tileID == 0) && (tid == 0)) {
    *g_counter = 0;
  }
}

// ----------------------------------------------------------------------------
