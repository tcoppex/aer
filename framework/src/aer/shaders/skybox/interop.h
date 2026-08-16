#ifndef SHADERS_SKYBOX_INTEROP_H_
#define SHADERS_SKYBOX_INTEROP_H_

// ----------------------------------------------------------------------------

const uint kAttribLocation_Position = 0;
const uint kAttribLocation_Normal   = 1;
const uint kAttribLocation_Texcoord = 2;

// ----------------------------------------------------------------------------

const uint kDescriptorSetBinding_Skybox_Sampler = 0;

// ----------------------------------------------------------------------------

const uint kDescriptorSetBinding_IntegrateBRDF_StorageImage = 2; //

const uint kCompute_IntegrateBRDF_kernelSize_x = 32u; //
const uint kCompute_IntegrateBRDF_kernelSize_y = 32u; //

// ----------------------------------------------------------------------------

// ----------------------------
// Technically we would need 2 MVPs for stereoscopical device, but it might
// outgrows a lower pushconstant limits (144 bytes < 128 bytes).
// So either we remove some bytes or we have to push a buffer descriptor.
// struct CameraData {
//   mat4 mvpMatrix[2];
// };
// ----------------------------

// 68 bytes < 128 bytes
struct PushConstant {
  // (skybox)
  mat4 mvpMatrix[1]; //
  float hdrIntensity;
  // -----
  // (integrated BRDF)
  uint mapResolution;
  uint numSamples;
  uint numElements;
};

#ifndef __cplusplus
#define GetModelViewProjMatrix() pushConstant.mvpMatrix[0*gl_ViewIndex]
#endif

// ----------------------------------------------------------------------------

#endif