#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

//-----------------------------------------------------------------------------
//
//  This implement one step of a simple bitonic sort, where:
//    * Input keys are float32 dot product values.
//    * Output sorted values are uint32 indices.
//
//-----------------------------------------------------------------------------

#include "../../interop.h"

//-----------------------------------------------------------------------------

layout(scalar, set = 0, binding = kDescriptorBinding_SBO_DotProducts)
readonly buffer SBO_dot_products_ {
  float Keys[];
};

layout(scalar, binding = kDescriptorBinding_SBO_Indices)
buffer SBO_indices_ {
  uint Indices[];
};

layout(push_constant, scalar)
uniform PushConstant_ {
  layout(offset = 64) PushConstant_Compute pushConstant;
};

//-----------------------------------------------------------------------------

void CompareAndSwap(in bool is_greater_than, inout uint left, inout uint right) {
  if (is_greater_than == (Keys[left] > Keys[right])) {
    left  ^= right;
    right ^= left;
    left  ^= right;
  }
}

//-----------------------------------------------------------------------------

layout(local_size_x = kCompute_SortIndex_kernelSize_x) in;

void main() {
  const uint gid = gl_GlobalInvocationID.x;

  if (gid >= pushConstant.numElems) {
    return;
  }

  const uint read_offset = pushConstant.readOffset;
  const uint write_offset = pushConstant.writeOffset;
  const uint block_width = pushConstant.blockWidth;
  const uint max_block_width = pushConstant.maxBlockWidth;

  const uint pair_distance = block_width / 2u;

  const uint block_offset = (gid / pair_distance) * block_width;
  const uint left_id = block_offset + (gid % pair_distance);
  const uint right_id = left_id + pair_distance;

  uint left_data  = Indices[read_offset + left_id];
  uint right_data = Indices[read_offset + right_id];

  const bool order = bool((left_id / max_block_width) & 1u);
  CompareAndSwap(order, left_data, right_data);

  Indices[write_offset + left_id]  = left_data;
  Indices[write_offset + right_id] = right_data;
}

//-----------------------------------------------------------------------------
