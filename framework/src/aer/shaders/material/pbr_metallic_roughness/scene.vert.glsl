#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------

#include <material/interop.h>
#include <material/pbr_metallic_roughness/interop.h>

// ----------------------------------------------------------------------------

layout(scalar, set = kDescriptorSet_Frame, binding = kDescriptorSet_Frame_FrameUBO)
uniform FrameUBO_ {
  FrameData uFrame;
};

layout(scalar, set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_TransformSBO)
buffer TransformSBO_ {
  TransformSBO transforms[];
};

layout(scalar, push_constant) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(location = kAttribLocation_Position) in vec3 inPosition;
layout(location = kAttribLocation_Normal  ) in vec3 inNormal;
layout(location = kAttribLocation_Texcoord) in vec2 inTexcoord;
layout(location = kAttribLocation_Tangent)  in vec4 inTangent;

layout(location = 0) out vec3 vPositionWS;
layout(location = 1) out vec3 vNormalWS;
layout(location = 2) out vec4 vTangentWS;
layout(location = 3) out vec2 vTexcoord;

// ----------------------------------------------------------------------------

void main() {
  TransformSBO transform = transforms[nonuniformEXT(pushConstant.transform_index)];
  mat4 worldMatrix = uFrame.default_world_matrix
                   * transform.worldMatrix
                   ;
  mat3 normalMatrix = mat3(worldMatrix);
  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);

  // -------

  gl_Position = GetFrameCamera(uFrame).viewProjMatrix * worldPos;
  vPositionWS = worldPos.xyz;
  vNormalWS   = normalize(normalMatrix * inNormal);
  vTangentWS  = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
  vTexcoord   = inTexcoord.xy;
}

// ----------------------------------------------------------------------------