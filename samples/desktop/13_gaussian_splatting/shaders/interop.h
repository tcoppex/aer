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

STATIC_CONST uint kCompute_Preprocess_kernelSize_x  = 256;
STATIC_CONST uint kCompute_PrefixSum_kernelSize_x   = 1024;

// ---------------------------------------------------------------------------

struct /*alignas(16)*/ GaussianData {
  float4 position;
  float4 rotation;
  float4 scale;
  float4 color;
};

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
  uint32_t numElems;
  uint32_t pad0_[1];
  // ----
  uint64_t uniform_addr;
  uint64_t gaussian_addr;
  uint64_t splat_addr;
  // ----
  uint64_t scan_input_addr;
  uint64_t scan_output_addr;
  uint64_t scan_output_group_addr; // (next input)
};

// ---------------------------------------------------------------------------

struct SplatOutput {
  float4 color;
  float3 conic;
  float depth;
  float2 screen_pos;
  uint32_t pad0_[2];
};

// ---------------------------------------------------------------------------

#endif