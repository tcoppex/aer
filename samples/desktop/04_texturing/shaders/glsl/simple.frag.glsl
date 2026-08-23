#version 460
#extension GL_GOOGLE_include_directive : require

// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

// Uniforms
layout(set = 0, binding = kDescriptorSetBinding_Sampler)
uniform sampler2D uSamplerAlbedo;

// Inputs
layout(location = 0)
in vec2 vTexcoord;

// Outputs
layout(location = 0)
out vec4 fragColor;

// ----------------------------------------------------------------------------

void main() {
  vec3 albedo = texture(uSamplerAlbedo, vTexcoord).xyz;
  fragColor = vec4(albedo, 1.0);
}

// ----------------------------------------------------------------------------