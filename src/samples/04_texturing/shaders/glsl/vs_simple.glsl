#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

/* A shader buffer act as a view to 1-to-n mapped memory buffers. */
layout(scalar, set = 0, binding = kDescriptorSetBinding_UniformBuffer) uniform UBO_ {
  UniformData uData;
};

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout (location = kAttribLocation_Position) in vec3 inPosition;
layout (location = kAttribLocation_Normal  ) in vec3 inNormal;
layout (location = kAttribLocation_Texcoord) in vec2 inTexcoord;

layout (location = 0) out vec2 vTexcoord;

// ----------------------------------------------------------------------------

void main() {
  mat4 worldMatrix = pushConstant.model.worldMatrix;

  mat4 modelViewProj = uData.scene.camera.projectionMatrix
                     * uData.scene.camera.viewMatrix
                     * worldMatrix
                     ;

  // Vertex outputs.
  // vNormal = normalize(mat3(worldMatrix) * inNormal);

  vTexcoord = inTexcoord.xy;
  vTexcoord = vec2(vTexcoord.x, 1.0 - vTexcoord.y);

  gl_Position = modelViewProj * vec4(inPosition, 1.0);
}

// ----------------------------------------------------------------------------