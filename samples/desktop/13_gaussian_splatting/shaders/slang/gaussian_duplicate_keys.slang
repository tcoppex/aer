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

  // Screen width resolution by tiles [could be a constant].
  let uData     = *((UniformBufferData*)pushConstant.uniform_addr);
  let tileResX  = uint32_t(ceil(uData.resolution.x / float(kTileResolution)));

  // Splat's preprocessed infos.
  let splat     = ((SplatOutput*)pushConstant.splat_addr)[gid];

  // Splat's tiles base offset.
  var offset    = ((uint32_t*)pushConstant.scan_output_addr)[gid];

  // Output pointers.
  uint64_t *out_key   = (uint64_t*)pushConstant.unsorted_keys_addr;
  uint32_t *out_value = (uint32_t*)pushConstant.unsorted_values_addr;

  for (var y = splat.minTile.y; y < splat.maxTile.y; ++y) {
    for (var x = splat.minTile.x; x < splat.maxTile.x; ++x) {
      let tileId = y * tileResX + x;

      if (offset < pushConstant.maxCapacity) {
        out_key[offset]   = (uint64_t(tileId) << 32) | uint64_t(asuint(splat.depth));
        out_value[offset] = gid;
      }
      ++offset;
    }
  }
}

// ----------------------------------------------------------------------------
