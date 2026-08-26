// ----------------------------------------------------------------------------

#include "../interop.h" //

static const uint kWorkgroupSize = kCompute_PrefixSum_kernelSize_x;
static const uint kMinWaveSize = 32;
static const uint kMaxWavesPerWorkgroup = kWorkgroupSize / kMinWaveSize;

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

groupshared uint32_t s_waveSums[kMaxWavesPerWorkgroup];

// ----------------------------------------------------------------------------

// Pass 1 : Up-Sweep (Local prefix sum).
// Dispatch ceil(numGaussians / kWorkgroupSize) workgroups

[shader("compute")]
[numthreads(kWorkgroupSize, 1, 1)]
void localPrefixSum(
  uint gid : SV_DispatchThreadID,   // global thread index
  uint tid : SV_GroupIndex,         // local thread index
  uint3 workgroupID : SV_GroupID,
)
{
  let groupID = workgroupID.x;

  uint val = 0;

  // When using subgroup we cannot have diverging branches (ie. can't return).
  if (gid < pushConstant.numElems) {
    let tileInfo = ((SplatTileInfo*)pushConstant.tile_addr)[gid];
    val = tileInfo.count;
  }

  // -----------------------------------------
  let laneCount     = WaveGetLaneCount();
  let waveCount     = (kWorkgroupSize + laneCount - 1) / laneCount;

  let wavePrefix    = WavePrefixSum(val); // offset inside the wave
  let waveSum       = WaveActiveSum(val);
  let waveID        = WaveGetWaveIndex();

  // Store the sum of each wave.
  if (WaveIsFirstLane()) {
    s_waveSums[waveID] = waveSum;
  }
  GroupMemoryBarrierWithGroupSync();

  // Calculate the Workgroup offset at the start of each wave.
  if (waveID == 0) {
    let waveVal = (tid < waveCount) ? s_waveSums[tid] : 0;
    s_waveSums[tid] = WavePrefixSum(waveVal);
  }
  GroupMemoryBarrierWithGroupSync();

  // local thread offset inside its workgroup.
  let localPrefix = wavePrefix + s_waveSums[waveID];
  // -----------------------------------------

  // Store the thread local offset.
  if (gid < pushConstant.numElems) {
    SplatTileInfo* tileInfo = &((SplatTileInfo*)pushConstant.tile_addr)[gid];
    tileInfo->offset = localPrefix;
  }

  // Store the WorkGroup last thread offset to continue the scan further more.
  if (tid == kWorkgroupSize - 1) {
    uint32_t* groupSums = (uint32_t*)pushConstant.workgroupSums_addr;
    groupSums[groupID] = localPrefix + val;
  }
}

// ----------------------------------------------------------------------------

// Pass 2 : Workgroup sums scan.

[shader("compute")]
[numthreads(kWorkgroupSize, 1, 1)]
void scanWorkgroupSums(
  uint gid : SV_DispatchThreadID,
  uint tid : SV_GroupIndex
)
{
  // uint val = 0;

  // if (gid < pushConstant.numElems) {
  //   uint32_t* groupSums = (uint32_t*)pushConstant.workgroupSums_addr;
  //   val = groupSums[gid];
  // }

  // TODO

  // if (gid < pushConstant.numElems) {
  //   uint32_t* scannedSums = (uint32_t*)pushConstant.scannedSums_addr;
  //   scannedSums[gid] = localSum;
  // }
}

// ----------------------------------------------------------------------------

// Pass 3 : Down-Sweep.
// dispatch ceil(numGaussians / kWorkgroupSize) workgroups

[shader("compute")]
[numthreads(kWorkgroupSize, 1, 1)]
void downSweep(
  uint gid : SV_DispatchThreadID
)
{
  if (gid >= pushConstant.numElems) {
    return;
  }

  // uint32_t* scannedSums = (uint32_t*)pushConstant.scannedSums_addr;
  // let globalBaseOffset = scannedSums[groupId];

  // SplatTileInfo* tileInfo = &((SplatTileInfo*)pushConstant.tile_addr)[gid];
  // tileInfo->offset += globalBaseOffset;
}

// ----------------------------------------------------------------------------
