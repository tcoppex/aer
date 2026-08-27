// ----------------------------------------------------------------------------

#include "../interop.h" //

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

[[vk::constant_id(0)]] const uint kWaveSize = 32;

static const uint kWorkgroupSize = kCompute_PrefixSum_kernelSize_x;
static const uint kMaxWavesPerWorkgroup = kWorkgroupSize / kWaveSize;

// ----------------------------------------------------------------------------

groupshared uint32_t s_waveSums[kMaxWavesPerWorkgroup];

// ----------------------------------------------------------------------------

uint blockScanAndExtractOffset(uint val, uint tid) {
  let laneCount     = WaveGetLaneCount();
  let waveCount     = (kWorkgroupSize + laneCount - 1) / laneCount;

  let wavePrefix    = WavePrefixSum(val); // offset inside the wave
  let waveSum       = WaveActiveSum(val);
  let waveID        = WaveGetWaveIndex();

  // Store the sum of each wave. (first thread of each wave)
  if (WaveIsFirstLane()) {
    s_waveSums[waveID] = waveSum;
  }
  GroupMemoryBarrierWithGroupSync();

  // Calculate the Workgroup offset at the start of each wave.
  if (waveID == 0) {
    let waveVal = (tid < waveCount) ? s_waveSums[tid] : 0;
    s_waveSums[tid] = WavePrefixSum(waveVal); //
  }
  GroupMemoryBarrierWithGroupSync();

  // local thread offset inside its workgroup.
  let localPrefix = wavePrefix + s_waveSums[waveID];

  return localPrefix;
}


// ----------------------------------------------------------------------------

// Pass 1 : Up-Sweep (Local prefix sum).
// Dispatch ceil(numGaussians / kWorkgroupSize) workgroups

[shader("compute")]
[numthreads(kWorkgroupSize, 1, 1)]
void localPrefixSum(
  uint3 workgroupID : SV_GroupID,         // workgroup coords
  uint gid : SV_DispatchThreadID,         // global thread index
  uint tid : SV_GroupIndex,               // local thread index
)
{
  uint val = 0;

  if (gid < pushConstant.numElems) {
    val = ((uint32_t*)pushConstant.scan_input_addr)[gid];
  }

  let localPrefix = blockScanAndExtractOffset(val, tid);

  // Store the thread local offset.
  if (gid < pushConstant.numElems) {
    uint32_t* localOffsets = ((uint32_t*)pushConstant.scan_output_local_addr);
    localOffsets[gid] = localPrefix;
  }

  // Store the WorkGroup last thread offset to continue the scan further more.
  if (tid == kWorkgroupSize - 1) {
    uint32_t* groupOffsets = ((uint32_t*)pushConstant.scan_output_group_addr);
    groupOffsets[workgroupID.x] = localPrefix + val;
  }
}

// ----------------------------------------------------------------------------

// Pass 2 : Down-Sweep.
// dispatch ceil(numGaussians / kWorkgroupSize) workgroups

[shader("compute")]
[numthreads(kWorkgroupSize, 1, 1)]
void addBlockOffsets(
  uint3 workgroupID : SV_GroupID,
  uint gid : SV_DispatchThreadID
)
{
  if (gid >= pushConstant.numElems) {
    return;
  }

  // Read the workgroup offset.
  uint32_t* groupOffsets = ((uint32_t*)pushConstant.scan_output_group_addr);
  let blockOffset = (workgroupID.x > 0) ? groupOffsets[workgroupID.x - 1] : 0;

  // Add it to the local element offset.
  uint32_t* localOffsets = ((uint32_t*)pushConstant.scan_output_local_addr);
  localOffsets[gid] += blockOffset;
}

// ----------------------------------------------------------------------------
