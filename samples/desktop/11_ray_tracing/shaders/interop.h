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

#elif defined(__SLANG__)

#define GetFrameData() \
  *(FrameData*)(pushConstant.frame_buffer_address)

#define GetRTGeometry() \
  rt_geometry::fetch_geometry( \
    pushConstant.geometry_table_address, InstanceID() + GeometryIndex() \
  )

// `geo` is a RTGeometryData (only valid if geo.material_address != 0)
#define GetRayTracingMaterial(geo) \
  ((RayTracingMaterial*)((geo).material_address))[(geo).material_index]

#endif

// -----------------------------------------------------------------------------

STATIC_CONST uint kDescriptorSetBinding_RayTracing_AccumImage   = 0;

// -----------------------------------------------------------------------------

struct PushConstant {
  uint64_t frame_buffer_address;
  uint64_t geometry_table_address;    // RTGeometryData[] (RayTracingView)
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

// [wip] RayTracing material.

STATIC_CONST uint kRayTracingMaterialType_Diffuse  = 0;
STATIC_CONST uint kRayTracingMaterialType_Mirror   = 1;
STATIC_CONST uint kRayTracingMaterialType_Emissive = 2;

// Mirrors fx::material::PBRMetallicRoughnessMaterial (the fx material SSBO).
// Any change on one side MUST be done on the other.
struct RayTracingMaterial {
  vec3 emissive_factor;
  uint emissive_texture_id;
  vec4 diffuse_factor;
  uint diffuse_texture_id;
  uint orm_texture_id;
  float metallic_factor;
  float roughness_factor;
  uint normal_texture_id;
  uint occlusion_texture_id;
  float alpha_cutoff;
  uint double_sided;
};

/* -------------------------------------------------------------------------- */

#endif // SHADERS_INTEROP_H_