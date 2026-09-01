// ----------------------------------------------------------------------------

#include "../radix_interop.h"

#define WAVE_SIZE     32  // [enforced]

#define NUM_THREADS   kRadixSize
#define NUM_PASSES    kRadixNumPasses
#define NUM_WAVES     ((NUM_THREADS) / (WAVE_SIZE))

// ----------------------------------------------------------------------------

[[vk::push_constant]]
RadixPushConstant pushConstant;

// ----------------------------------------------------------------------------

#define NOT_READY   0
#define AGGREGATE   1
#define PREFIX      2
#define NUM_FLAGS   3

[ForceInline]
uint32_t atomicLoad(uint32_t *ptr) {
  uint32_t prev = 0;
  InterlockedOr(*ptr, 0, prev);
  return prev;
}

[ForceInline]
uint32_t atomicAdd(uint32_t *ptr, uint32_t value) {
  uint32_t prev = 0;
  InterlockedAdd(*ptr, value, prev);
  return prev;
}

[ForceInline]
void atomicExchange(uint32_t *ptr, uint32_t val) {
  InterlockedExchange(*ptr, val);
}

// ----------------------------------------------------------------------------

groupshared uint32_t s_tileID;
groupshared uint32_t s_digitCounts[NUM_WAVES][NUM_THREADS];
groupshared uint32_t s_digitTotal[NUM_THREADS];
groupshared uint32_t s_tileExclusivePrefix[NUM_THREADS];

// ----------------------------------------------------------------------------

[ForceInline]
uint32_t extractDigit(uint64_t key, uint32_t pass) {
  return uint32_t((key >> (pass * kRadixBits)) & (NUM_THREADS - 1));
}

uint32_t waveMatchMask(uint32_t digit, bool isValid) {
  uint32_t mask = 0xFFFFFFFFu;

  [unroll]
  for (uint32_t b = 0; b < kRadixBits; ++b) {
    bool bitSet = ((digit >> b) & 1u) != 0;
    let ballot = WaveActiveBallot(bitSet).x;
    mask &= bitSet ? ballot : ~ballot;
  }

  return mask & WaveActiveBallot(isValid).x;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(NUM_THREADS, 1, 1)]
void main(
  uint tid : SV_GroupIndex
)
{
  let g_digitOffsets  = (uint32_t*)pushConstant.histogram_addr;

  let g_descriptor    = (uint32_t*)pushConstant.descriptor_addr;
  let g_counter       = (uint32_t*)pushConstant.counter_addr;

  let g_keysIn        = (uint64_t*)pushConstant.unsorted_keys_addr;
  let g_valuesIn      = (uint32_t*)pushConstant.unsorted_values_addr;

  let g_keysOut       = (uint64_t*)pushConstant.sorted_keys_addr;
  let g_valuesOut     = (uint32_t*)pushConstant.sorted_values_addr;

  // ---------------

  if (tid == 0) {
    s_tileID = atomicAdd(g_counter, 1u);
  }

  // Clear the per-wave table
  [unroll]
  for (uint i = 0; i < NUM_WAVES; ++i) {
    s_digitCounts[i][tid] = 0;
  }
  GroupMemoryBarrierWithGroupSync();

  // ---------------

  let numElems          = *((uint32_t*)pushConstant.numkeys_addr); //

  let tileID            = s_tileID;
  let globalIndex       = tileID * NUM_THREADS + tid;
  let isValid           = globalIndex < numElems; //

  var key   : uint64_t  = 0;
  var value : uint32_t  = 0;

  if (isValid) {
    key   = g_keysIn[globalIndex];
    value = g_valuesIn[globalIndex];
  }

  let digit = extractDigit(key, pushConstant.pass);

  let laneIndex = WaveGetLaneIndex();
  let waveIndex = WaveGetWaveIndex();
  let matchMask = waveMatchMask(digit, isValid);
  let laneMask  = (1u << laneIndex) - 1u;

  let waveRank  = countbits(matchMask & laneMask);
  let waveCount = countbits(matchMask);

  if (isValid && (firstbitlow(matchMask) == laneIndex)) {
    s_digitCounts[waveIndex][tid] = waveCount;
  }
  GroupMemoryBarrierWithGroupSync();
}

// ----------------------------------------------------------------------------
