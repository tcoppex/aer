#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include <skybox/interop.h>

// ----------------------------------------------------------------------------

layout(location = 0) in vec3 inView;

layout(location = 0) out vec4 fragColor;

layout (set = 0, binding = kDescriptorSetBinding_Skybox_Sampler)
uniform samplerCube uCubemap;

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

void main() {
  const float factor = pushConstant.hdrIntensity;
  fragColor = factor * textureLod(uCubemap, inView, 0.0);
}

// ----------------------------------------------------------------------------
