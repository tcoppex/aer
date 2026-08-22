#ifndef SHADERS_INTEROP_H_
#define SHADERS_INTEROP_H_

/* -------------------------------------------------------------------------- */

#if defined(_GLSL_)
#extension GL_EXT_ray_tracing : require
#endif // defined(_GLSL_)

// ----------------------------------------------------------------------------

#if !defined(STATIC_CONST)

#if defined(_GLSL_)
#define STATIC_CONST const
#else
#define STATIC_CONST static const
#endif

#endif

// -----------------------------------------------------------------------------

#if defined(_GLSL_) || defined(__SLANG__)
#include <material/interop.h>
#endif

// -----------------------------------------------------------------------------

#if defined(_GLSL_)

// (redefine the one from push_constant_generic.h)
#define GetFrameData() \
  FrameBufferRef(pushConstant.frame_buffer_address) \
    .uFrameData

#define GetRTInstanceData() \
  InstanceDataBufferRef(pushConstant.instance_buffer_address) \
    .instances[nonuniformEXT(gl_InstanceID)]

#elif defined(__SLANG__)

#define GetFrameData() \
  *(FrameData*)(pushConstant.frame_buffer_address)

#define GetRTInstanceData() \
  *(RTInstanceData*)(pushConstant.instance_buffer_address)

#endif

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
  vec3 origin;
  vec3 direction;
  vec3 radiance;
  vec3 throughput;
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
  vec3 emissive_factor;
  uint emissive_texture_id;
  vec4 diffuse_factor;
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