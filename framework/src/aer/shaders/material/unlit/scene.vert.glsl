#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------

#include <material/interop.h>
#include <material/unlit/interop.h>

#include <shared/maths.glsl> // (for calculate_reorient_matrix)

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
layout(location = kAttribLocation_Texcoord) in vec3 inTexcoord;
layout(location = 0) out vec3 vPositionWS;
layout(location = 1) out vec2 vTexcoord;

// ----------------------------------------------------------------------------

layout(constant_id = 1) const int constant_kBillboardMode = 0;

void apply_billboard_xz(
  in mat4 worldMatrix,
  in mat3 normalMatrix,
  in mat4 viewMatrix,
  in mat4 invViewMatrix,
  inout vec3 localPos,
  inout vec3 localNor
) {
  /// + Billboard effect +
  ///
  /// Change the angle of the Y axis to face the screen/camera direction.
  /// <!> Only work on **Y-upward** object-space quads <!>
  ///

  const vec3 kDefaultUp    = vec3(0, 1, 0);
  const vec3 kDefaultFront = vec3(0, 0, 1);

  // To remove the axis we will turn around to from the camera direction.
  const vec3 kUpComplement = vec3(1.0) - abs(kDefaultUp);

  // Reorient the (Y-upward) mesh to face +Z.
  const vec3 newNormal = /*normalize*/(normalMatrix * kDefaultFront);
  const mat3 reorientMatrix = calculate_reorient_matrix(localNor, newNormal);
  localPos = reorientMatrix * localPos;
  localNor = reorientMatrix * localNor; // (for quads, use newNormal directly)

  // Calculate a base to face the camera.
  mat3 viewBaseXZ;

  if (constant_kBillboardMode == 1)
  {
    // Face the Screen.

    const vec3 right = normalize(vec3(viewMatrix[0][0], 0.0, viewMatrix[2][0]));
    const vec3 front = normalize(vec3(viewMatrix[0][2], 0.0, viewMatrix[2][2]));

    // (alternative version)
    // const mat3 viewT = transpose(mat3(viewMatrix));
    // const vec3 right = normalize(viewT[0] * kUpComplement);
    // const vec3 front = normalize(viewT[2] * kUpComplement);

    viewBaseXZ = mat3(right, kDefaultUp, front);
  }
  else if (constant_kBillboardMode == 2)
  {
    // Face the Camera.

    const vec3 centerWS = (worldMatrix * vec4(0,0,0,1)).xyz;
    const vec3 camPos = invViewMatrix[3].xyz;
    const vec3 camDir = normalize(centerWS - camPos);

    const vec3 front = normalize(-camDir * kUpComplement);
    const vec3 right = normalize(cross(kDefaultUp, front));

    viewBaseXZ = mat3(right, kDefaultUp, front);
  }

  localPos = viewBaseXZ * localPos;
  localNor = viewBaseXZ * localNor;
}

// ----------------------------------------------------------------------------

void main() {
  const CameraTransform camera = GetFrameCamera(uFrame);
  const TransformSBO transform = transforms[nonuniformEXT(pushConstant.transform_index)];

  const mat4 worldMatrix = uFrame.default_world_matrix
                         * transform.worldMatrix
                         ;
  const mat3 normalMatrix = mat3(worldMatrix);

  vec3 localPos = inPosition;
  vec3 localNor = /*normalize*/(inNormal);

  if (constant_kBillboardMode > 0)
  {
    apply_billboard_xz(
      worldMatrix,
      normalMatrix,
      camera.viewMatrix,
      camera.invViewMatrix,
      localPos,
      localNor
    );
  }

  const vec4 worldPos = worldMatrix * vec4(localPos, 1.0);
  const vec3 worldNor = normalize(normalMatrix * localNor);

  gl_Position = camera.viewProjMatrix * worldPos;

  vPositionWS = worldPos.xyz;
  // vNormalWS   = worldNor.xyz;
  vTexcoord   = inTexcoord.xy;
}

// ----------------------------------------------------------------------------
