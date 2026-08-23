#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

layout(set = 0, binding = kDescriptorSetBinding_Sampler)
uniform sampler2D[] uTexturesMap;

layout(set = 0, binding = kDescriptorSetBinding_IrradianceEnvMap)
uniform samplerCube uIrradianceEnvMap;

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant pushConstant;
};

layout (location = 0) in vec2 vTexcoord;
layout (location = 1) in vec3 vNormal;

layout (location = 0) out vec4 fragColor;

// ----------------------------------------------------------------------------

void main() {
  vec4 diffuse = texture(uTexturesMap[pushConstant.model.albedo_texture_index], vTexcoord);
  vec3 irradiance = texture(uIrradianceEnvMap, vNormal).rgb;

  vec3 ambient = 0.5
               * irradiance
               * diffuse.rgb
               ;

  fragColor = vec4(ambient.xyz, 1.0f);
}

// ----------------------------------------------------------------------------