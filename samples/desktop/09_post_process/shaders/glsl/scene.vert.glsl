#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

layout(scalar, set = 0, binding = kDescriptorSetBinding_UniformBuffer)
uniform UBO_ {
  UniformData uData;
};

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout (location = kAttribLocation_Position) in vec3 inPosition;
layout (location = kAttribLocation_Normal  ) in vec3 inNormal;
layout (location = kAttribLocation_Texcoord) in vec2 inTexcoord;

layout (location = 0) out vec2 vTexcoord;
layout (location = 1) out vec3 vNormal;
layout (location = 2) out vec3 vWorldPosition;

// ----------------------------------------------------------------------------

void main() {
  mat4 worldMatrix = pushConstant.model.worldMatrix;

  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);

  vTexcoord      = inTexcoord.xy;
  vNormal        = normalize(mat3(worldMatrix) * inNormal);
  vWorldPosition = worldPos.xyz;

  mat4 viewProjMatrix = uData.scene.projectionMatrix
                      * pushConstant.viewMatrix
                      ;

  gl_Position = viewProjMatrix * worldPos;
}

// ----------------------------------------------------------------------------