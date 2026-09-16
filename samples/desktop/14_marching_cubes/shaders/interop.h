#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// -----------------------------------------------------------<----------------

#ifdef __cplusplus
#define ALIGNAS(x)  alignas(16)
#else
#define ALIGNAS(x)
#endif

// -----------------------------------------------------------<----------------

static const uint32_t kCompute_BuildDensity_kernelSize  = 4;
static const uint32_t kCompute_MaxLinearGroupSize       = 256; //

static const uint32_t kDescriptorSetBinding_SamplerNearest            = 0;
static const uint32_t kDescriptorSetBinding_SamplerLinear             = 1;
static const uint32_t kDescriptorSetBinding_DensityTexture_Storage    = 2;
static const uint32_t kDescriptorSetBinding_DensityTexture_Sampling   = 3;
static const uint32_t kDescriptorSetBinding_VertexIndicesVolume       = 4;

// -----------------------------------------------------------<----------------

static const uint32_t kChunkDim         = 24u;
static const uint32_t kChunkMargin      = 4u;
static const uint32_t kWindowDim        = kChunkDim + 2 * kChunkMargin;
static const uint32_t kVoxelsPerSlice   = kChunkDim * kChunkDim;
static const uint32_t kVoxelsPerChunk   = kChunkDim * kVoxelsPerSlice;

static const float kChunkSize           = 12.0f;

static const float kInvChunkDim         = 1.0 / float(kChunkDim);
static const float kInvChunkDimMinusOne = 1.0 / float(kChunkDim - 1);

static const float kDensityVolumeTexRes = kWindowDim;
static const float kTexelSize           = 1.0 / kDensityVolumeTexRes;

// ---------------------------------------------------------------------------

static const uint32_t ATOMIC_COUNT_CELL = 0u;
static const uint32_t ATOMIC_COUNT_VERT = 1u;
static const uint32_t ATOMIC_COUNT_INDX = 2u;

// ---------------------------------------------------------------------------

struct UniformBufferData {
  float4x4 viewMatrix;
  float4x4 projectionMatrix;
  float2 tanFov;
  float2 focal;
  float2 resolution;
  uint32_t pad0_[2];
};

struct PushConstant {
  uint3 gridSize;
  uint32_t atomicCountIndex;
  float4 chunkAttributes;
  // ---
  uint64_t nonEmptyCellsBuffer;
  uint64_t verticesToGenerateBuffer;
  uint64_t atomicCountBuffer;

  uint64_t indirectBuffer;

  uint64_t indicesBuffer;
  uint64_t verticesBuffer;
};

// ---------------------------------------------------------------------------

#endif