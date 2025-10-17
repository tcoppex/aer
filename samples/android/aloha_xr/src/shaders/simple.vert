#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_multiview : require

// ----------------------------------------------------------------------------

#include "interop.h"

// ----------------------------------------------------------------------------

layout(scalar, set = 0, binding = 0) uniform UBO_ {
  UniformCameraData uCamera[2];
};

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout (location = 0) in vec4 inPosition;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

// ----------------------------------------------------------------------------

void main() {
  UniformCameraData view = uCamera[gl_ViewIndex];
  
  mat4 viewProjMatrix = view.projectionMatrix
                      * view.viewMatrix
                      ;
  mat4 worldMatrix = pushConstant.modelMatrix;

  outColor = inColor;

  gl_Position = viewProjMatrix
              * worldMatrix
              * inPosition
              ;
}

// ----------------------------------------------------------------------------
