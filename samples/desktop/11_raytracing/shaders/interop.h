#ifndef SHADER_INTEROP_H_
#define SHADER_INTEROP_H_

/* -------------------------------------------------------------------------- */

#ifndef __cplusplus

#include <material/interop.h>
// #include <material/push_constant_generic.h> // (not compatible yet)

// (redefine the one from push_constant_generic.h)
#define GetFrameData() FrameBufferRef(pushConstant.frame_buffer_address).uFrameData

#endif

// -----------------------------------------------------------------------------

const uint kDescriptorSetBinding_Sample11_AccumImage   = 0;
const uint kDescriptorSetBinding_Sample11_MaterialSBO  = 1; // (to remove)

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

struct PushConstant {
  uint64_t frame_buffer_address;
  uint64_t material_buffer_address;
  // ----
  int accumulation_frame_count;
  int num_samples;
  float jitter_factor;
  float light_intensity;
  float sky_intensity;
  uint _pad0[3];
};

// -----------------------------------------------------------------------------

// Simple RayTracing proxy material.

const uint kRayTracingMaterialType_Diffuse  = 0;
const uint kRayTracingMaterialType_Mirror   = 1;
const uint kRayTracingMaterialType_Emissive = 2;

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

/* -------------------------------------------------------------------------- */

#endif // SHADER_INTEROP_H_