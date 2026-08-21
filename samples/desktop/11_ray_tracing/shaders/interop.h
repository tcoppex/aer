#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

/* -------------------------------------------------------------------------- */

#if defined(_GLSL_)

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_nonuniform_qualifier : enable

#define float4x4  mat4
#define float4    vec4
#define float3    vec3

#define STATIC_CONST const

#include <material/interop.h>
// #include <material/push_constant_generic.h> // (not compatible yet)

// (redefine the one from push_constant_generic.h)
#define GetFrameData() \
  FrameBufferRef(pushConstant.frame_buffer_address) \
    .uFrameData

#define GetInstanceData() \
  InstanceDataBufferRef(pushConstant.instance_buffer_address) \
    .instances[nonuniformEXT(gl_InstanceID)];

#else

#define STATIC_CONST static const

#endif // defined(_GLSL_)


// -----------------------------------------------------------------------------

STATIC_CONST uint kDescriptorSetBinding_RayTracing_AccumImage   = 0;
STATIC_CONST uint kDescriptorSetBinding_RayTracing_MaterialSBO  = 1;

// -----------------------------------------------------------------------------

struct PushConstant {
  uint64_t frame_buffer_address;
  uint64_t material_buffer_address;
  uint64_t instance_buffer_address;
  uint64_t tlas_address;
  // ----
  int accumulation_frame_count;
  int num_samples;
  float jitter_factor;
  float light_intensity;
  float sky_intensity;
  uint _pad0[3];
};

// -----------------------------------------------------------------------------

struct HitPayload_t {
  float3 origin;
  float3 direction;
  float3 radiance;
  float3 throughput;
  int done;
  int depth;
  uint rngState;
};

// -----------------------------------------------------------------------------

// Simple RayTracing proxy material.

STATIC_CONST uint kRayTracingMaterialType_Diffuse  = 0;
STATIC_CONST uint kRayTracingMaterialType_Mirror   = 1;
STATIC_CONST uint kRayTracingMaterialType_Emissive = 2;

struct RayTracingMaterial {
  float3 emissive_factor;
  uint emissive_texture_id;
  float4 diffuse_factor;
  uint diffuse_texture_id;
  uint orm_texture_id;
  float metallic_factor;
  float roughness_factor;
  float alpha_cutoff;
  uint _pad0[3];
};

// -----------------------------------------------------------------------------

struct RTInstanceData {
  uint64_t vertexAddr;
  uint64_t indexAddr;
};

/* -------------------------------------------------------------------------- */

#endif // SHADERS_INTEROP_H_