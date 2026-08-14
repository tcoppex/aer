#ifndef SHADERS_ENVMAP_INTEROP_H_
#define SHADERS_ENVMAP_INTEROP_H_

// ----------------------------------------------------------------------------

const uint kAttribLocation_Position = 0;
const uint kAttribLocation_Normal   = 1;
const uint kAttribLocation_Texcoord = 2;

// ----------------------------------------------------------------------------

const uint kDescriptorSetBinding_Sampler                            = 0;
const uint kDescriptorSetBinding_StorageImage                       = 1;
const uint kDescriptorSetBinding_StorageImageArray                  = 2;

const uint kDescriptorSetBinding_IrradianceSHCoeff_StorageBuffer    = 3;
const uint kDescriptorSetBinding_IrradianceSHMatrices_StorageBuffer = 4;

// ----------------------------------------------------------------------------

const uint kCompute_SphericalTransform_kernelSize_x = 16u;
const uint kCompute_SphericalTransform_kernelSize_y = 16u;

const uint kCompute_IrradianceSHCoeff_kernelSize_x = 16u;
const uint kCompute_IrradianceSHCoeff_kernelSize_y = 16u;

const uint kCompute_IrradianceReduceSHCoeff_kernelSize_x = 256u;

const uint kCompute_Irradiance_kernelSize_x = 16u;
const uint kCompute_Irradiance_kernelSize_y = 16u;

const uint kCompute_Specular_kernelSize_x = 16u;
const uint kCompute_Specular_kernelSize_y = 16u;

// ----------------------------------------------------------------------------

/*--
* We only need mat3[3] - or vec3[9] - plus one float for sumWeight, but
* to be aligned we use vec4 instead, and data[0].w for sumWeight.
* --*/
struct SHCoeff {
  vec4 data[9];
};

struct SHMatrices {
  mat4 data[3];
};

// ----------------------------------------------------------------------------

// [92 bytes < 128 bytes]
struct PushConstant {
  mat4 viewProjectionMatrix;
  uint mapResolution;
  uint numSamples;
  uint mipLevel;
  float roughnessSquared;
  //
  uint numElements;
  uint readOffset;
  uint writeOffset;
};

// ----------------------------------------------------------------------------

#endif