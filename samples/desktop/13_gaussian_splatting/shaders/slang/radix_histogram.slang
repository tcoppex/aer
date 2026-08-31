// ----------------------------------------------------------------------------

#include "../radix_interop.h"

#define NUM_THREADS kRadixHistogramNumThreads
#define NUM_PASSES  kRadixDigitCount

// ----------------------------------------------------------------------------

STATIC_CONST uint32_t kItemsPerThreads  = 16;
STATIC_CONST uint32_t kTileSize         = NUM_THREADS * kItemsPerThreads;

// ----------------------------------------------------------------------------

[[vk::push_constant]]
RadixPushConstant pushConstant;

groupshared uint32_t s_Histogram[kRadixHistogramSize];

// ----------------------------------------------------------------------------

[ForceInline]
uint extractDigit(uint64_t key, uint pass) {
  return uint((key >> (pass * kRadixBits)) & (kRadixSize - 1));
}

[ForceInline]
uint histogramIndex(uint pass, uint digit) {
  return pass * kRadixSize + digit;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(NUM_THREADS, 1, 1)]
void main(
  uint3 groupID   : SV_GroupID,
  uint3 threadID  : SV_GroupThreadID
)
{
  let tid       = threadID.x;
  let tileBase  = groupID.x * NUM_THREADS;
  let numKeys   = *((uint32_t*)pushConstant.numkeys_addr);

  uint32_t *g_keys        = ((uint32_t*)pushConstant.unsorted_keys_addr);
  uint32_t *g_histograms  = ((uint32_t*)pushConstant.histogram_addr);

  // 1. Initialize local histogram (8 per threads)
  for (uint i = tid; i < kRadixHistogramSize; i += NUM_THREADS) {
    s_Histogram[i] = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // 2. Fill block's shared histogram (kItemsPerThreads per threads).
  for (uint i = tid; i < kTileSize; i+= NUM_THREADS) {
    let index = tileBase + i;

    if (index < numKeys) {
      let key = g_keys[index];

      [unroll]
      for (uint pass = 0; pass < NUM_PASSES; ++pass) {
        let digit = extractDigit(key, pass);
        InterlockedAdd(s_Histogram[histogramIndex(pass, digit)], 1u);
      }
    }
  }
  GroupMemoryBarrierWithGroupSync();

  // 3. Add the result to the global histogram.
  for (uint i = tid; i < kRadixHistogramSize; i += NUM_THREADS) {
    let h = s_Histogram[i];
    if (h != 0) {
      InterlockedAdd(g_histograms[i], h);
    }
  }
}

// ----------------------------------------------------------------------------
