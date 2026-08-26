#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

// ---------------------------------------------------------------------------
//
// Reference:
//   "3D Gaussian Splatting for Real-Time Radiance Field Rendering"
//   Bernhard Kerbl, Georgios Kopanas, Thomas Leimkühler, George Drettakis
//   ACM Transactions on Graphics (TOG), Vol. 42, No. 4, July 2023
//   https://repo-sam.inria.fr/fungraph/3d-gaussian-splatting/
//
// see also
//   https://github.com/graphdeco-inria/diff-gaussian-rasterization
//
// ---------------------------------------------------------------------------

#define STATIC_CONST static const

// ---------------------------------------------------------------------------

STATIC_CONST uint kCompute_Preprocess_kernelSize_x = 256;

// ---------------------------------------------------------------------------

struct /*alignas(16)*/ GaussianData {
  float4 position;
  float4 rotation;
  float4 scale;
  float4 color;
};

// ---------------------------------------------------------------------------

// > 128bytes,
// would need to separate the matrices in a UBO

struct PushConstant {
  float4x4 viewMatrix;
  float4x4 projectionMatrix;
  // ----
  float4 position;
  float2 tanFov;
  float2 focal;
  float2 resolution;
  uint64_t gaussian_addr_;
  uint64_t splat_addr_;
  uint32_t numElems;
};

// ---------------------------------------------------------------------------

struct SplatOutput {
  float2 position2D;
  float depth;
  float3 conic;
  uint32_t colorPacked;
  uint2 tileBounds;
};

// ---------------------------------------------------------------------------

#endif