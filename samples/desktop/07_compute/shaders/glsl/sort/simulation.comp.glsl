#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include "../../interop.h"

// ----------------------------------------------------------------------------

layout(scalar, set = 0, binding = kDescriptorBinding_SBO_Positions)
buffer SBO_positions_ {
  vec4 Positions[];
};

layout(push_constant, scalar)
uniform PushConstant_ {
  layout(offset = 64) PushConstant_Compute pushConstant;
};

// ----------------------------------------------------------------------------

vec3 wave(in vec3 pt, float wavelength, float amplitude, float speed, in vec2 direction) {
  direction = normalize(direction);

  const float w = 2.0f / wavelength;
  const float param = dot(pt.xz, direction) * w + speed * pushConstant.time;
  const vec2 crest = direction * cos(param) * (1.0f / w);

  return vec3(
    crest.x,
    amplitude * sin(param),
    crest.y
  );
}

// ----------------------------------------------------------------------------

layout(local_size_x = kCompute_Simulation_kernelSize_x) in;

void main() {
  const uint gid = gl_GlobalInvocationID.x;

  if (gid >= pushConstant.numElems) {
    return;
  }

  // We fetch the original particle position on the back buffer, as we will
  // update their xz position too (not just their height).
  const vec4 particle = Positions[pushConstant.numElems + gid];
  vec3 pos = particle.xyz;

  // --------------------------------

  float W = 2.50f;
  float A = 0.20f;
  float S = 2.10f;

  pos = wave(pos, W, A, S, vec2(1.0, 0.0))
      + wave(pos, W * 0.250f, A * 0.50f, S * 1.10f, vec2(1.0, -0.2))
      + wave(pos, W * 0.125f, A * 0.25f, S * 1.21f, vec2(0.8, +0.5))
      + wave(pos, W * 0.500f, A * 0.75f, S * 0.90f, vec2(0.8, +3.1))
      ;
  pos.xz = particle.xz + pos.xz / 4.0f;

  float size = 5.0f * (1.0f - smoothstep(-0.65f, +0.65f, pos.y));

  // --------------------------------

  Positions[gid] = vec4(pos, size);
}

// ----------------------------------------------------------------------------
