#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_multiview : require

// ----------------------------------------------------------------------------

#include <skybox/interop.h>

// ----------------------------------------------------------------------------

layout(location = 0) in vec3 inPosition;

layout(location = 0) out vec3 outView;

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

void main() {
  const vec4 clip_pos = pushConstant.mvpMatrix[gl_ViewIndex]
                      * vec4(inPosition, 1.0)
                      ;

  // Use the box object coordinates in [-1, 1] as texture coordinates.
  outView = normalize(2.0f * inPosition);

  // Assure depth value is always at 1.0.
  gl_Position = clip_pos.xyww;
}

// ----------------------------------------------------------------------------
