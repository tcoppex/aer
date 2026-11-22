#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include <envmap/interop.h>
#include <shared/constants.glsl>

// ----------------------------------------------------------------------------

layout(scalar, binding = kDescriptorSetBinding_IrradianceSHCoeff_StorageBuffer)
readonly buffer SHCoeffData_ {
  SHCoeff sh_coeff[];
};

layout(scalar, binding = kDescriptorSetBinding_IrradianceSHMatrices_StorageBuffer)
writeonly buffer SHMatrixData_ {
  SHMatrices sh_matrices;
};

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(
  local_size_x = 1
) in;

// ----------------------------------------------------------------------------

void main() {
  /* The last writeOffset is the current read offset. */
  SHCoeff shc = sh_coeff[pushConstant.writeOffset];

  const float total_weight = 4.0 / shc.data[0].w;

  for (int i = 0; i < 9; ++i) {
    shc.data[i] *= total_weight;
  }

  const float c1 = 0.429043f;
  const float c2 = 0.511664f;
  const float c3 = 0.743125f;
  const float c4 = 0.886227f;
  const float c5 = 0.247708f;

  for (int i = 0; i < 3; ++i) {
    sh_matrices.data[i] = mat4(
      vec4(
        c1 * shc.data[8][i],
        c1 * shc.data[4][i],
        c1 * shc.data[7][i],
        c2 * shc.data[3][i]
      ),

      vec4(
        c1 * shc.data[4][i],
       -c1 * shc.data[8][i],
        c1 * shc.data[5][i],
        c2 * shc.data[1][i]
      ),

      vec4(
        c1 * shc.data[7][i],
        c1 * shc.data[5][i],
        c3 * shc.data[6][i],
        c2 * shc.data[2][i]
      ),

      vec4(
        c2 * shc.data[3][i],
        c2 * shc.data[1][i],
        c2 * shc.data[2][i],
        c4 * shc.data[0][i] - c5 * shc.data[6][i]
      )
    );
  }
}

// ----------------------------------------------------------------------------
