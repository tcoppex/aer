// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_Duplicate_kernelSize_x, 1, 1)]
void main(
  uint gid : SV_DispatchThreadID
)
{
  if (gid >= pushConstant.numElems) {
    return;
  }

  // memory limit, not actual registered total count.
  let KV_LIMIT = pushConstant.maxKeyValueCapacity;

  // Discard culled gaussians.
  let count     = ((uint32_t*)pushConstant.scan_input_addr)[gid];
  var offset    = ((uint32_t*)pushConstant.scan_output_addr)[gid];
  if ((offset >= KV_LIMIT) || (count == 0)) {
    return;
  }

  // Screen width resolution by tiles [could be a constant].
  let uData     = *((UniformBufferData*)pushConstant.uniform_addr);
  let tileResX  = uint32_t((uData.resolution.x + kTileResolution - 1) / kTileResolution); //

  // Splat preprocessed data.
  let splat     = ((SplatOutput*)pushConstant.splat_addr)[gid];
  let depthId   = uint64_t(asuint(splat.depth));

  // Output pointers.
  let out_key   = (uint64_t*)pushConstant.unsorted_keys_addr;
  let out_value = (uint32_t*)pushConstant.unsorted_values_addr;

  for (var y = splat.minTile.y; y < splat.maxTile.y; ++y) {
    for (var x = splat.minTile.x; x < splat.maxTile.x; ++x) {
      let tileId = y * tileResX + x;
      if (offset < KV_LIMIT) {
        out_key[offset]   = (uint64_t(tileId) << 32) | depthId;
        out_value[offset] = gid;
      }
      ++offset;
    }
  }
}

// ----------------------------------------------------------------------------
