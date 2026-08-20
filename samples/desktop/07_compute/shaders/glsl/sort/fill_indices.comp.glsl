#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

//-----------------------------------------------------------------------------

#include "../../interop.h"

//-----------------------------------------------------------------------------

layout(scalar, binding = kDescriptorBinding_SBO_Indices)
writeonly buffer SBO_indices_ {
  uint Indices[];
};

layout(push_constant, scalar)
uniform PushConstant_ {
  layout(offset = 64) PushConstant_Compute pushConstant;
};

//-----------------------------------------------------------------------------

layout(local_size_x = kCompute_FillIndex_kernelSize_x) in;

void main() {
  const uint gid = gl_GlobalInvocationID.x;

  if (gid >= pushConstant.numElems) {
    return;
  }

  Indices[gid] = gid;
}

//-----------------------------------------------------------------------------
