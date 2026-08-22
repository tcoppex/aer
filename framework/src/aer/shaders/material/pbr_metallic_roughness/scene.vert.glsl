#version 460

// ----------------------------------------------------------------------------

#include <material/pbr_metallic_roughness/interop.h>

// ----------------------------------------------------------------------------

layout(buffer_reference, scalar)
readonly buffer FrameBufferRef {
  FrameData uFrameData;
};

layout(buffer_reference, scalar)
readonly buffer TransformBufferRef {
  TransformData transforms[];
};

layout(scalar, push_constant)
uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(location = kAttribLocation_Position) in vec3 inPosition;
layout(location = kAttribLocation_Normal)   in vec3 inNormal;
layout(location = kAttribLocation_Tangent)  in vec4 inTangent;
layout(location = kAttribLocation_Texcoord) in vec2 inTexcoord;

layout(location = 0) out vec3 vPositionWS;
layout(location = 1) out vec3 vNormalWS;
layout(location = 2) out vec4 vTangentWS;
layout(location = 3) out vec2 vTexcoord;

// ----------------------------------------------------------------------------

void main() {
  const FrameData frameData = GetFrameData();
  const CameraData camera = GetCameraData(frameData, gl_ViewIndex);
  const TransformData transform = GetTransform();

  // -------

  mat4 worldMatrix = frameData.default_world_matrix
                   * transform.worldMatrix
                   ;
  mat3 normalMatrix = mat3(worldMatrix);
  vec4 worldPos = worldMatrix * vec4(inPosition, 1.0);

  // -------

  gl_Position = camera.viewProjMatrix * worldPos;
  vPositionWS = worldPos.xyz;
  vNormalWS   = normalize(normalMatrix * inNormal);
  vTangentWS  = vec4(normalize(normalMatrix * inTangent.xyz), inTangent.w);
  vTexcoord   = inTexcoord.xy;
}

// ----------------------------------------------------------------------------