#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

#include "../../interop.h"

// ----------------------------------------------------------------------------

layout(scalar, set = 0, binding = kDescriptorBinding_UBO_Data)
uniform UBO_ {
  UniformData uData;
};

layout(scalar, set = 0, binding = kDescriptorBinding_SBO_Positions)
readonly buffer SBO_positions_ {
  vec4 WorldPositions[];
};

layout(scalar, set = 0, binding = kDescriptorBinding_SBO_DotProducts)
writeonly buffer SBO_dot_products_ {
  float DotProducts[];
};

layout(push_constant, scalar)
uniform PushConstant_ {
  layout(offset = 64) PushConstant_Compute pushConstant;
};

// The default front of camera in view space.
const vec3 kTargetVS = vec3(0.0f, 0.0f, -1.0f);

// ----------------------------------------------------------------------------

layout(local_size_x = kCompute_DotProduct_kernelSize_x) in;

void main() {
  const uint gid = gl_GlobalInvocationID.x;

  if (gid >= pushConstant.numElems) {
    return;
  }

  // Transform a particle's position to view space.
  vec4 positionVS = uData.scene.camera.viewMatrix
                  * pushConstant.model.worldMatrix
                  * vec4(WorldPositions[gid].xyz, 1.0f)
                  ;

  // Distance of the particle from the camera.
  DotProducts[gid] = dot(kTargetVS, positionVS.xyz);
}

// ----------------------------------------------------------------------------