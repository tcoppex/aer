#ifndef SHADERS_SCENE_INTEROP_H_
#define SHADERS_SCENE_INTEROP_H_

// ----------------------------------------------------------------------------
// -- Vertex Inputs --

const uint kAttribLocation_Position = 0;
const uint kAttribLocation_Normal   = 1;
const uint kAttribLocation_Texcoord = 2;
const uint kAttribLocation_Tangent  = 3;

struct Vertex {
  vec3 position; float _pad0[1];
  vec3 normal;   float _pad1[1];
  vec4 tangent;
  vec2 texcoord; float _pad2[2];
};

// ----------------------------------------------------------------------------
// -- Descriptor Sets --

// set index as used for MaterialFx and bindings as defined in descriptor_set_registry.

const uint kDescriptorSet_Internal = 0;

const uint kDescriptorSet_Frame = 1;
const uint kDescriptorSet_Frame_FrameUBO            = 0;

const uint kDescriptorSet_Scene = 2;
const uint kDescriptorSet_Scene_TransformSBO        = 0;
const uint kDescriptorSet_Scene_Textures            = 1;
const uint kDescriptorSet_Scene_IBL_Prefiltered     = 2;
const uint kDescriptorSet_Scene_IBL_Irradiance      = 3;
const uint kDescriptorSet_Scene_IBL_SpecularBRDF    = 4;

const uint kDescriptorSet_RayTracing = 3;
const uint kDescriptorSet_RayTracing_TLAS           = 0;
const uint kDescriptorSet_RayTracing_InstanceSBO    = 1;

// ----------------------------------------------------------------------------
// -- Utility structs & constants --

// [hacky] the order *must* match the Camera::Transform struct.
struct CameraTransform {
  mat4 projectionMatrix;
  mat4 invProjectionMatrix;
  mat4 viewMatrix;
  mat4 invViewMatrix;
  mat4 viewProjMatrix;
};

const uint kRendererState_IrradianceBit = 0x1 << 0;

// ----------------------------------------------------------------------------
// -- Uniform Buffer(s) --

struct FrameData {
  CameraTransform camera[2];
  mat4 default_world_matrix;
  vec4 cameraPos_Time;   // xxx
  vec2 resolution;
  uint frame;
  uint renderer_states; // (wip)
};

#ifndef __cplusplus
#extension GL_EXT_multiview : require
#define GetFrameCamera(FrameData) FrameData.camera[gl_ViewIndex]
#endif

// ----------------------------------------------------------------------------
// -- Storage Buffer(s) --

struct TransformSBO {
  mat4 worldMatrix;
};

// ----------------------------------------------------------------------------

#endif // SHADERS_SCENE_INTEROP_H_
