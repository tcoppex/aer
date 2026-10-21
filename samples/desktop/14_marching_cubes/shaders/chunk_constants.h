#ifndef SHADERS_CHUNK_CONSTANTS_H_
#define SHADERS_CHUNK_CONSTANTS_H_

// ----------------------------------------------------------------------------

#ifdef __cplusplus
#define ALIGNAS(x)  alignas(x)
#else
#define ALIGNAS(x)
#endif

struct ALIGNAS(16) MarchingCubeVertex {
  float3 position;
  float ao;
  float3 normal;
  float unused;
};

// ----------------------------------------------------------------------------

static const uint32_t kChunkMaxVertices = (1u << 14u);      // 16384
static const uint32_t kChunkMaxIndices  = (1u << 16u) - 1u; // 65535 (multiple of 3)
static const uint32_t kChunkDim         = 24u;

static const uint32_t kChunkMargin      = 4u;
static const float kChunkSize           = 12.0f;

// ----------------------------------------------------------------------------

static const uint32_t kWindowDim        = kChunkDim + 2 * kChunkMargin;
static const uint32_t kVoxelsPerSlice   = kChunkDim * kChunkDim;
static const uint32_t kVoxelsPerChunk   = kChunkDim * kVoxelsPerSlice;

static const float kInvChunkDim         = 1.0 / float(kChunkDim);
static const float kInvChunkDimMinusOne = 1.0 / float(kChunkDim - 1);
static const float kDensityVolumeTexRes = kWindowDim;
static const float kTexelSize           = 1.0 / kDensityVolumeTexRes;

// ----------------------------------------------------------------------------

#endif // SHADERS_CHUNK_CONSTANTS_H_