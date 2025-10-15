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

const uint kRendererState_IrradianceBit = 0x1 << 0; //

// ----------------------------------------------------------------------------

struct FrameData {
  mat4 projectionMatrix;
  mat4 invProjectionMatrix;
  mat4 viewMatrix;
  mat4 invViewMatrix;
  mat4 viewProjMatrix;
  vec4 cameraPos_Time;   // xyz = camera, w = time
  vec2 resolution;
  uint frame;
  uint renderer_states; //
};

struct TransformSBO {
  mat4 worldMatrix;
};

// ----------------------------------------------------------------------------

#endif