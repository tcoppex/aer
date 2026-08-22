#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include <envmap/interop.h>

// ----------------------------------------------------------------------------

layout(
  local_size_x = kCompute_IrradianceReduceSHCoeff_kernelSize_x
) in;

layout(scalar, binding = kDescriptorSetBinding_IrradianceSHCoeff_StorageBuffer)
buffer SHCoeffData_ {
  SHCoeff sh_coeff_data[];
};

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

shared vec4 sharedData[kCompute_IrradianceReduceSHCoeff_kernelSize_x];

void main() {
  const uint localId = gl_LocalInvocationID.x;
  const uint globalId = gl_GlobalInvocationID.x;
  const uint groupSize = gl_WorkGroupSize.x;
  const uint groupId = gl_WorkGroupID.x;

  if (globalId >= pushConstant.numElements) {
    return;
  }

  SHCoeff local_sh = sh_coeff_data[pushConstant.readOffset + globalId];

  for (int cid = 0; cid < 9; ++cid) {
    sharedData[localId] = local_sh.data[cid];
    barrier();

    for (uint offset = groupSize/2; offset > 0; offset >>= 1) {
      if (localId < offset) {
        sharedData[localId] += sharedData[localId + offset];
      }
      barrier();
    }

    if (localId == 0) {
      local_sh.data[cid] = sharedData[0];
    }
  }

  if (localId == 0) {
    sh_coeff_data[pushConstant.writeOffset + groupId] = local_sh;
  }
}

// ----------------------------------------------------------------------------
