#ifndef SHADERS_SKYBOX_INTEROP_H_
#define SHADERS_SKYBOX_INTEROP_H_

// ----------------------------------------------------------------------------

#ifdef __cplusplus
#define UINT uint32_t
#else
#define UINT uint
#endif

// ----------------------------------------------------------------------------

const UINT kAttribLocation_Position = 0;
const UINT kAttribLocation_Normal   = 1;
const UINT kAttribLocation_Texcoord = 2;

// ----------------------------------------------------------------------------

const UINT kDescriptorSetBinding_Skybox_Sampler = 0;

// ----------------------------------------------------------------------------

const UINT kDescriptorSetBinding_IntegrateBRDF_StorageImage = 2; //

const UINT kCompute_IntegrateBRDF_kernelSize_x = 32u; //
const UINT kCompute_IntegrateBRDF_kernelSize_y = 32u; //

// ----------------------------------------------------------------------------

// 80 bytes
struct PushConstant {
  mat4 mvpMatrix[2];
  float hdrIntensity;
  UINT mapResolution;
  UINT numSamples;
  UINT numElements;
};

// ----------------------------------------------------------------------------

#undef UINT

#endif