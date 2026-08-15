#ifndef SHADER_INTEROP_H_
#define SHADER_INTEROP_H_

/* -------------------------------------------------------------------------- */

const uint kDescriptorSetBinding_ImageOutput      = 0;
const uint kDescriptorSetBinding_MaterialSBO      = 1;

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