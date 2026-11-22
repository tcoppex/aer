#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------
//
// Calculate the irradiance convolution of an environment map.
//
// ----------------------------------------------------------------------------

#include <envmap/interop.h>
#include <shared/maths.glsl>

// ----------------------------------------------------------------------------

layout (set = 0, binding = kDescriptorSetBinding_Sampler)
uniform samplerCube inDiffuseEnvmap;

layout(rgba16f, binding = kDescriptorSetBinding_StorageImage)
writeonly
uniform imageCube outIrradianceEnvmap;

layout(scalar, binding = kDescriptorSetBinding_IrradianceSHMatrices_StorageBuffer)
readonly buffer SHMatrixData_ {
  SHMatrices uIrradianceMatrices;
};

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(
  local_size_x = kCompute_Irradiance_kernelSize_x,
  local_size_y = kCompute_Irradiance_kernelSize_y,
  local_size_z = 1
) in;

void main() {
  const ivec3 coords = ivec3(gl_GlobalInvocationID.xyz);
  const int resolution = int(pushConstant.mapResolution);

  if (!all(lessThan(coords, ivec3(resolution, resolution, 6)))) {
    return;
  }

  // --------------

  const vec4 n = vec4(cubemap_view_direction(coords, resolution), 1.0); //

  // (the 2nd order SH approximation can ring below zero near strong lights)
  const vec3 irradiance = max(vec3(
    dot( n, uIrradianceMatrices.data[0] * n),
    dot( n, uIrradianceMatrices.data[1] * n),
    dot( n, uIrradianceMatrices.data[2] * n)
  ), vec3(0.0));

  // --------------

  imageStore(outIrradianceEnvmap, coords, vec4(irradiance, 1.0));
}

// ----------------------------------------------------------------------------
