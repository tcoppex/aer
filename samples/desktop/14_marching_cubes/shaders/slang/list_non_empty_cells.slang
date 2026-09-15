#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

// [vk::buffer_reference]
// struct CellBuffer {
//   uint data[];
// };

// [vk::buffer_reference]
// struct AtomicCounter {
//   uint value;
// };

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSetBinding_SamplerNearest, 0)]]
SamplerState uSamplerNearest : register(s0);

[[vk::binding(kDescriptorSetBinding_DensityTexture_Sampling, 0)]]
Texture3D<float> uDensityTexture : register(t0);

float sampleNearest(float3 uvw) {
  return uDensityTexture.SampleLevel(uSamplerNearest, uvw, 0.0).r;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(
  kCompute_BuildDensity_kernelSize,
  kCompute_BuildDensity_kernelSize,
  kCompute_BuildDensity_kernelSize
)]
void main(uint3 coords : SV_DispatchThreadID)
{
  if (any(coords >= pushConstant.gridSize)) {
    return;
  }

  let uvw = kTexelSize * (float3(coords) + float3(kChunkMargin));

  let offset = float2(kTexelSize, 0.0f);

  let sideA = float4(
    sampleNearest(uvw + offset.yyy),
    sampleNearest(uvw + offset.yxy),
    sampleNearest(uvw + offset.xxy),
    sampleNearest(uvw + offset.xyy)
  );
  let sideB = float4(
    sampleNearest(uvw + offset.yyx),
    sampleNearest(uvw + offset.yxx),
    sampleNearest(uvw + offset.xxx),
    sampleNearest(uvw + offset.xyx)
  );

  let iA = uint4(step(0.0f, sideA));
  let iB = uint4(step(0.0f, sideB));

  let cube_case = (iA.x << 0) | (iA.y << 1) | (iA.z << 2) | (iA.w << 3)
                | (iB.x << 4) | (iB.y << 5) | (iB.z << 6) | (iB.w << 7);

  if ((cube_case > 0) && (cube_case < 255)) {
    let g_counter = ((uint32_t*)pushConstant.atomicCountBuffer);
    let g_cells   = ((uint32_t*)pushConstant.nonEmptyCellsBuffer);

    uint out_index;
    InterlockedAdd(*g_counter, 1u, out_index);

    g_cells[out_index] = ((coords.x & 0xFFU) << 0)
                       | ((coords.y & 0xFFU) << 8)
                       | ((coords.z & 0xFFU) << 16)
                       | ((cube_case & 0xFFU) << 24)
                       ;
  }
}

// ----------------------------------------------------------------------------
