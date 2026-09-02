// ----------------------------------------------------------------------------
//
// Reference:
//   " Single-pass Parallel Prefix Scan with Decoupled Look-back "
//      from Duane Merrill & Michael Garland
//
// ----------------------------------------------------------------------------

#include "../interop.h" //

#define NUM_THREADS kCompute_PrefixSum_kernelSize_x

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

static const uint32_t kWaveSize = 32; // [enforced on the CPU]
static const uint32_t kMaxWavesPerWorkgroup = NUM_THREADS / kWaveSize;

// ----------------------------------------------------------------------------

groupshared uint32_t s_waveSums[kMaxWavesPerWorkgroup];
groupshared uint32_t s_tileID;
groupshared uint32_t s_tileAggregate;
groupshared uint32_t s_tileExclusivePrefix;

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

uint32_t blockScanAndExtractOffset(uint32_t val, uint32_t tid) {
  let laneCount   = WaveGetLaneCount();
  let waveCount   = (NUM_THREADS + laneCount - 1) / laneCount;

  let wavePrefix  = WavePrefixSum(val);
  let waveSum     = WaveActiveSum(val);
  let waveID      = WaveGetWaveIndex();

  // Store the sum of each wave. (first thread of each wave)
  if (WaveIsFirstLane()) {
    s_waveSums[waveID] = waveSum;
  }
  GroupMemoryBarrierWithGroupSync();

  // Calculate the Workgroup offset at the start of each wave.
  if (waveID == 0) {
    let waveVal = (tid < waveCount) ? s_waveSums[tid] : 0;
    s_waveSums[tid] = WavePrefixSum(waveVal);

    // Block-wide total
    let total = WaveActiveSum(waveVal);
    if (tid == 0) {
      s_tileAggregate = total;
    }
  }
  GroupMemoryBarrierWithGroupSync();

  return wavePrefix + s_waveSums[waveID];
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(NUM_THREADS, 1, 1)]
void main(uint tid : SV_GroupIndex)
{
  let g_descriptor  = (uint32_t*)pushConstant.scan_descriptor_addr;
  let g_counter     = (uint32_t*)pushConstant.scan_counter_addr;
  let g_scan_input  = (uint32_t*)pushConstant.scan_input_addr;
  let g_scan_output = (uint32_t*)pushConstant.scan_output_addr;

  if (tid == 0) {
    s_tileID = atomicAdd(g_counter, 1u);
  }
  GroupMemoryBarrierWithGroupSync();

  let tileID      = s_tileID;
  let baseIndex   = tileID * NUM_THREADS;
  let globalIndex = baseIndex + tid;

  let val = (globalIndex < pushConstant.numElems)
    ? g_scan_input[globalIndex]
    : 0;
  let localPrefix = blockScanAndExtractOffset(val, tid);
  let localAggregate = s_tileAggregate;

  uint32_t *tileDescriptor = g_descriptor + tileID * NUM_FLAGS;

  if (tileID == 0) {
    if (globalIndex < pushConstant.numElems) {
      g_scan_output[globalIndex] = localPrefix;
    }
    if (tid == 0) {
      tileDescriptor[PREFIX] = localAggregate;
      AllMemoryBarrier();
      atomicExchange(tileDescriptor, PREFIX);
    }
    return;
  }

  if (tid == 0) {
    tileDescriptor[AGGREGATE] = localAggregate;
    AllMemoryBarrier();
    atomicExchange(tileDescriptor, AGGREGATE);

    uint lookbackIndex = tileID - 1;
    uint exclusivePrefix = 0;

    while (true) {
      uint32_t* prevDescriptor = g_descriptor + lookbackIndex * NUM_FLAGS;
      let flag = atomicLoad(prevDescriptor);

      if (flag == PREFIX)
      {
        exclusivePrefix += atomicLoad(&prevDescriptor[PREFIX]);
        break;
      }
      else if (flag == AGGREGATE)
      {
        exclusivePrefix += atomicLoad(&prevDescriptor[AGGREGATE]);
        --lookbackIndex;
      }
    }

    s_tileExclusivePrefix = exclusivePrefix;

    tileDescriptor[2] = exclusivePrefix + localAggregate;
    AllMemoryBarrier();
    atomicExchange(tileDescriptor, PREFIX);
  }
  GroupMemoryBarrierWithGroupSync();

  if (globalIndex < pushConstant.numElems) {
    let scanResult = localPrefix + s_tileExclusivePrefix;
    g_scan_output[globalIndex] = scanResult;
  }
}

// ----------------------------------------------------------------------------
