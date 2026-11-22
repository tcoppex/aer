#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------

#include <skybox/interop.h>
#include <shared/tonemapping.glsl>

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
  const vec3 radiance = pushConstant.hdrIntensity
                      * textureLod(uCubemap, inView, 0.0).rgb
                      ;

  const vec3 color = tonemapDisplayLinear(
    radiance,
    pushConstant.exposure,
    int(pushConstant.tonemapping)
  );

  fragColor = vec4(color, 1.0);
}

// ----------------------------------------------------------------------------
