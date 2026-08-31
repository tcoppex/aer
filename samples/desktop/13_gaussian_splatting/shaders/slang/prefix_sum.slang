// ----------------------------------------------------------------------------
//
// Reference:
//   " Single-pass Parallel Prefix Scan with Decoupled Look-back "
//      from Duane Merrill & Michael Garland
//
// ----------------------------------------------------------------------------

#include "../interop.h" //

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

[[vk::constant_id(0)]]
const uint32_t kWaveSize = 32; // TODO

static const uint32_t kWorkgroupSize = kCompute_PrefixSum_kernelSize_x;
static const uint32_t kMaxWavesPerWorkgroup = kWorkgroupSize / kWaveSize;

// ----------------------------------------------------------------------------

groupshared uint32_t s_waveSums[kMaxWavesPerWorkgroup];
groupshared uint32_t s_tileID;
groupshared uint32_t s_prevInclusivePrefix;

// ----------------------------------------------------------------------------

enum DescFlag {
  NOT_READY,
  AGGREGATE,
  PREFIX,
};

DescFlag atomicLoad(uint32_t *ptr) {
  return (DescFlag)((Atomic<uint32_t>*)ptr)->load();
}

void atomicExchange(uint32_t *ptr, DescFlag flag) {
  ((Atomic<uint32_t>*)ptr)->exchange((uint32_t)flag);
}

uint32_t atomicAdd(uint32_t *ptr, uint32_t value) {
  return ((Atomic<uint32_t>*)ptr)->add(value);
}

// ----------------------------------------------------------------------------

uint32_t blockScanAndExtractOffset(uint32_t val, uint32_t tid) {
  let laneCount   = WaveGetLaneCount();
  let waveCount   = (kWorkgroupSize + laneCount - 1) / laneCount;

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
    s_waveSums[tid] = WavePrefixSum(waveVal); //

    let total = WaveActiveSum(waveVal);
    if (tid == 0) {
      s_prevInclusivePrefix = total;
    }
  }
  GroupMemoryBarrierWithGroupSync();

  return wavePrefix + s_waveSums[waveID];
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kWorkgroupSize, 1, 1)]
void main(uint tid : SV_GroupIndex)
{
  if (tid == 0) {
    s_tileID = atomicAdd(((uint32_t*)pushConstant.scan_counter_addr), 1u);
  }
  GroupMemoryBarrierWithGroupSync();

  let tileID      = s_tileID;
  let baseIndex   = tileID * kWorkgroupSize;
  let globalIndex = baseIndex + tid;
  let laneCount   = WaveGetLaneCount();

  let val = (globalIndex < pushConstant.numElems)
    ? ((uint32_t*)pushConstant.scan_input_addr)[globalIndex]
    : 0;
  let localPrefix = blockScanAndExtractOffset(val, tid);

  // Updated by blockScanAndExtractOffset.
  let localAggregate = s_prevInclusivePrefix;

  uint32_t *tileDescriptor = ((uint32_t*)pushConstant.scan_descriptor_addr)
                           + tileID * 2;

  if (tileID == 0) {
    if (globalIndex < pushConstant.numElems) {
      ((uint32_t*)pushConstant.scan_output_addr)[globalIndex] = localPrefix;
    }
    if (tid == 0) {
      tileDescriptor[1] = localAggregate;
      AllMemoryBarrier();
      atomicExchange(tileDescriptor, DescFlag::PREFIX);
    }
    return;
  }

  if (tid == 0) {
    tileDescriptor[1] = localAggregate;
    AllMemoryBarrier();
    atomicExchange(tileDescriptor, DescFlag::AGGREGATE);

    uint lookbackIndex = tileID - 1;
    uint exclusivePrefix = 0;

    while (true) {
      uint32_t* prevDescriptor = ((uint32_t*)pushConstant.scan_descriptor_addr)
                               + lookbackIndex * 2;

      let flag = atomicLoad(prevDescriptor);

      if (flag == DescFlag::PREFIX)
      {
        exclusivePrefix += prevDescriptor[1];
        break;
      }
      else if (flag == DescFlag::AGGREGATE)
      {
        exclusivePrefix += prevDescriptor[1];
        lookbackIndex--;
      }
    }

    s_prevInclusivePrefix = exclusivePrefix;

    tileDescriptor[1] = exclusivePrefix + localAggregate;
    AllMemoryBarrier();
    atomicExchange(tileDescriptor, DescFlag::PREFIX);
  }
  GroupMemoryBarrierWithGroupSync();

  if (globalIndex < pushConstant.numElems) {
    let scanResult = localPrefix + s_prevInclusivePrefix;
    ((uint32_t*)pushConstant.scan_output_addr)[globalIndex] = scanResult;
  }
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(1, 1, 1)]
void resetTotalCountIndirect()
{
  let lastId      = pushConstant.numElems - 1;
  let lastCount   = ((uint32_t*)pushConstant.scan_input_addr)[lastId];
  let lastOffset  = ((uint32_t*)pushConstant.scan_output_addr)[lastId];

  uint32_t *indirectBuffer = (uint32_t*)pushConstant.scan_total_count_indirect_addr;

  let totalCount = lastOffset + lastCount;
  let kRadixNumThread = 256; // kRadixHistogramNumThreads

  // [TODO: use custom struct instead]
  indirectBuffer[0] = (totalCount + kRadixNumThread - 1) / kRadixNumThread; //
  indirectBuffer[1] = 1;
  indirectBuffer[2] = 1;
  indirectBuffer[3] = totalCount;
}

// ----------------------------------------------------------------------------
