// ----------------------------------------------------------------------------

#include "../radix_interop.h"

#define WAVE_SIZE     32  // [enforced]

#define NUM_THREADS   kRadixSize
#define NUM_WAVES     ((NUM_THREADS) / (WAVE_SIZE))

// ----------------------------------------------------------------------------

[[vk::push_constant]]
RadixPushConstant pushConstant;

// ----------------------------------------------------------------------------

groupshared uint32_t s_waveSums[NUM_THREADS]; //

// ----------------------------------------------------------------------------

[ForceInline]
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
    let prefix = WavePrefixSum(waveVal);
    if (tid < waveCount) {
      s_waveSums[tid] = prefix;
    }
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
  let g_histograms = (uint32_t*)pushConstant.histogram_addr;
  let h_index = groupID.x * NUM_THREADS + tid;

  let count   = g_histograms[h_index];
  let offset  = blockScanAndExtractOffset(count, tid);

  // Update the histogram in-place.
  g_histograms[h_index] = offset;
}

// ----------------------------------------------------------------------------
