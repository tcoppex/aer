#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------
//
// Calculate the irradiance spherical harmonics coefficients of an environment map.
//
// cf. Ravi Ramamoorthi & Pat Hanrahan's technique as described in their 2001 paper
//    "An Efficient Representation for Irradiance Environment Maps"
//
// ----------------------------------------------------------------------------

#include <envmap/interop.h>
#include <shared/maths.glsl>

// ----------------------------------------------------------------------------

layout (set = 0, binding = kDescriptorSetBinding_Sampler)
uniform samplerCube inDiffuseEnvmap;

layout(scalar, binding = kDescriptorSetBinding_IrradianceSHCoeff_StorageBuffer)
writeonly buffer SHCoeffBuffer_ {
  SHCoeff OutSHCoeffs[];
};

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

float area(float x, float y) {
  return atan(x * y, sqrt(x * x + y * y + 1.0f));
}

float calculate_solid_angle(float u, float v, float texelSize) {
  float x0 = u - texelSize;
  float y0 = v - texelSize;
  float x1 = u + texelSize;
  float y1 = v + texelSize;
  float solidAngle = (area(x0,y0) + area(x1,y1))
                   - (area(x0,y1) + area(x1,y0))
                   ;
  return solidAngle;
}

// ----------------------------------------------------------------------------

layout(
  local_size_x = kCompute_IrradianceSHCoeff_kernelSize_x,
  local_size_y = kCompute_IrradianceSHCoeff_kernelSize_y,
  local_size_z = 1
) in;

void main() {
  const ivec3 coords = ivec3(gl_GlobalInvocationID);
  const int resolution = int(pushConstant.mapResolution);

  if (!all(lessThan(coords, ivec3(resolution, resolution, 1)))) {
    return;
  }

  const ivec3 gridDim   = ivec3(gl_NumWorkGroups);
  const ivec3 blockDim  = ivec3(gl_WorkGroupSize);

  const int gid = coords.x
                + coords.y * gridDim.x * blockDim.x
                + coords.z * gridDim.x * blockDim.x * gridDim.y * blockDim.y
                ;

  // --------------

  const float texelSize = 1.0f / float(resolution);
  const float u = (float(coords.x) + 0.5f) * texelSize;
  const float v = (float(coords.y) + 0.5f) * texelSize;

  // The solid angle is defined on the cube face in [-1, 1]^2, where a texel
  // is centered on (2u-1, 2v-1) and has an half extent of 1/resolution.
  const float solidAngle = calculate_solid_angle(
    2.0f * u - 1.0f,
    2.0f * v - 1.0f,
    texelSize
  );

  SHCoeff shc;
  for (int i = 0; i < 9; ++i) {
    shc.data[i] = vec4(0.0);
  }

  for (int faceId = 0; faceId < 6; ++faceId) {
    const vec3 n = cubemap_view_direction(u, v, faceId);
    const vec3 radiance = texture(inDiffuseEnvmap, n).rgb;
    const vec3 lambda = radiance * solidAngle;

    shc.data[0].xyz += lambda * 0.282095f;
    shc.data[1].xyz += lambda * 0.488603f * n.y;
    shc.data[2].xyz += lambda * 0.488603f * n.z;
    shc.data[3].xyz += lambda * 0.488603f * n.x;
    shc.data[4].xyz += lambda * 1.092548f * n.x * n.y;;
    shc.data[5].xyz += lambda * 1.092548f * n.y * n.z;
    shc.data[6].xyz += lambda * 0.315392f * (3.0f * n.z * n.z - 1.0f);
    shc.data[7].xyz += lambda * 1.092548f * n.x * n.z;
    shc.data[8].xyz += lambda * 0.546274f * (n.x * n.x - n.y * n.y);

    shc.data[0].w += solidAngle;
  }

  // --------------

  OutSHCoeffs[gid] = shc;
}

// ----------------------------------------------------------------------------
