#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

layout(scalar, set = 0, binding = kDescriptorBinding_UBO_Data)
uniform UBO_ {
  UniformData uData;
};

layout(scalar, set = 0, binding = kDescriptorBinding_SBO_Positions)
readonly buffer SBO_positions_ {
  vec4 Positions[];
};

layout(scalar, set = 0, binding = kDescriptorBinding_SBO_Indices)
readonly buffer SBO_indices_ {
  uint Indices[];
};

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant_Graphics pushConstant;
};

// ----------------------------------------------------------------------------

const vec2 kLocalOffsets[4] = vec2[](
  vec2(-0.5,  0.5),
  vec2(-0.5, -0.5),
  vec2( 0.5,  0.5),
  vec2( 0.5, -0.5)
);

const vec2 kLocalTexCoords[4] = vec2[](
  vec2(0.0, 1.0),
  vec2(0.0, 0.0),
  vec2(1.0, 1.0),
  vec2(1.0, 0.0)
);

// ----------------------------------------------------------------------------

layout(location = 0) out vec2 vTexCoord;
layout(location = 1) out vec3 vPosition;

// ----------------------------------------------------------------------------

void main() {
  mat4 worldMatrix = pushConstant.model.worldMatrix;

  mat4 viewMatrix = uData.scene.camera.viewMatrix;

  mat4 viewProj = uData.scene.camera.projectionMatrix
                * viewMatrix
                ;

  // --------------------------------

  int primitive_id = gl_InstanceIndex;
  int vertex_id = gl_VertexIndex;

  vec4 particle = Positions[Indices[primitive_id]];
  vec3 center = particle.xyz;
  vec2 spriteSize = vec2(0.15f * particle.w);

  vec2 quad_offset = spriteSize * kLocalOffsets[vertex_id];

  /* Create a 'billboard' quad from the particle position. */
  vec3 quad_right = normalize(vec3(viewMatrix[0].x, viewMatrix[1].x, viewMatrix[2].x));
  vec3 quad_up    = normalize(vec3(viewMatrix[0].y, viewMatrix[1].y, viewMatrix[2].y));

  vec3 vertex_offset = quad_right * quad_offset.x
                     + quad_up * quad_offset.y
                     ;
  const vec3 worldPos = vec3(worldMatrix * vec4(center, 1.0f)) + vertex_offset;

  // --------------------------------

  vPosition = worldPos;
  vTexCoord = kLocalTexCoords[vertex_id];

  gl_Position = viewProj * vec4(worldPos, 1.0f);
}

// ----------------------------------------------------------------------------
