#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------

#include "../interop.h"

#include <shared/maths.glsl>

// ----------------------------------------------------------------------------

layout(set = 0, binding = kDescriptorSetBinding_Sampler)
uniform sampler2D[] uTextureChannels;

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant pushConstant;
};

layout (location = 0) in vec2 vTexcoord;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec3 vWorldPosition;

layout (location = 0) out vec4 fragColor;
layout (location = 1) out vec4 fragData;

// ----------------------------------------------------------------------------

void main() {
  vec4 diffuse = texture(uTextureChannels[pushConstant.model.albedo_texture_index], vTexcoord);

  if (diffuse.w < 0.499f) {
    discard;
  }

  float alpha = diffuse.w;
  fragColor = vec4(diffuse.xyz, alpha);

  float depth = length(vWorldPosition - pushConstant.cameraPosition);

  fragData.xy = encodeNormal(normalize(vNormal));
  fragData.z = depth;
  fragData.w = intBitsToFloat(int(pushConstant.model.instance_index) + 1);
}

// ----------------------------------------------------------------------------