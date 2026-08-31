// ----------------------------------------------------------------------------

#include "../radix_interop.h"

#define NUM_THREADS kRadixSize // <=> kRadixHistogramNumThreads

// ----------------------------------------------------------------------------

[[vk::push_constant]]
RadixPushConstant pushConstant;

static const uint32_t kWaveSize = 32;
static const uint32_t kMaxWavesPerWorkgroup = NUM_THREADS / kWaveSize;

// ----------------------------------------------------------------------------

groupshared uint32_t s_waveSums[kMaxWavesPerWorkgroup];

// ----------------------------------------------------------------------------

uint32_t blockScanAndExtractOffset(uint32_t val, uint32_t tid) {
  let laneCount   = WaveGetLaneCount();
  let waveCount   = (NUM_THREADS + laneCount - 1) / laneCount;

  let wavePrefix  = WavePrefixSum(val);
  let waveSum     = WaveActiveSum(val);
  let waveID      = WaveGetWaveIndex();

  if (WaveIsFirstLane()) {
    s_waveSums[waveID] = waveSum;
  }
  GroupMemoryBarrierWithGroupSync();

  if (waveID == 0) {
    let waveVal = (tid < waveCount) ? s_waveSums[tid] : 0;
    s_waveSums[tid] = WavePrefixSum(waveVal); //
  }
  GroupMemoryBarrierWithGroupSync();

  return wavePrefix + s_waveSums[waveID];
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(NUM_THREADS, 1, 1)]
void main(
  uint3 groupID : SV_GroupID,
  uint tid : SV_GroupIndex
)
{
  let digit   = groupID.x;
  let offset  = digit * NUM_THREADS;
  let h_index = offset + tid;

  uint val = 0;

  if (h_index < kRadixHistogramSize) {
    val = ((uint32_t*)pushConstant.histogram_addr)[h_index];
  }

  let localPrefix = blockScanAndExtractOffset(val, tid);

  if (h_index < kRadixHistogramSize) {
    uint32_t *prefixes = (uint32_t*)pushConstant.histogram_prefixes_addr;
    prefixes[h_index] = localPrefix;
  }
}

// ----------------------------------------------------------------------------
