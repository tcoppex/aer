#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------

#include <material/interop.h>
#include <material/unlit/interop.h>

// ----------------------------------------------------------------------------

layout(constant_id = 0) const bool constant_kUseAlphaCutoff = false;

// ----------------------------------------------------------------------------

layout(scalar, set = kDescriptorSet_Internal, binding = kDescriptorSet_Internal_MaterialSBO)
buffer MaterialSSBO_ {
  Material materials[];
};

layout(scalar, set = kDescriptorSet_Frame, binding = kDescriptorSet_Frame_FrameUBO)
uniform FrameUBO_ {
  FrameData uFrame;
};

layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_Textures)
uniform sampler2D[] uTextureChannels;

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(location = 0) in vec3 vPositionWS;
layout(location = 1) in vec2 vTexcoord;

layout(location = 0) out vec4 fragColor;

// ----------------------------------------------------------------------------

#define TEXTURE_ATLAS(i)  uTextureChannels[nonuniformEXT(i)]

vec4 sample_DiffuseColor(in Material mat) {
  return texture(TEXTURE_ATLAS(mat.diffuse_texture_id), vTexcoord).rgba;
}

// ----------------------------------------------------------------------------

void main() {
  Material mat = materials[nonuniformEXT(pushConstant.material_index)];

  const vec4 mainColor = sample_DiffuseColor(mat)
                       * mat.diffuse_factor
                       ;

  if (constant_kUseAlphaCutoff)
  {
    if (mainColor.a < mat.alpha_cutoff) {
      discard;
    }
  }

  fragColor = mainColor;
}

// ----------------------------------------------------------------------------