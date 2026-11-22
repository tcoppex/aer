#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------
//
// Calculate the specular prefiltered convolution of an environment map.
//
// Increasing level of the cubemap correspond to a higher rough reflection level.
//
// Ref for optimizations :
//  * https://placeholderart.wordpress.com/2015/07/28/implementation-notes-runtime-environment-map-filtering-for-image-based-lighting/
//  * https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
//  * https://www.tobias-franke.eu/log/2014/03/30/notes_on_importance_sampling.html
//
// ----------------------------------------------------------------------------

#include <envmap/interop.h>
#include <shared/maths.glsl>
#include <shared/lighting/pbr.glsl>

// ----------------------------------------------------------------------------

layout(set = 0, binding = kDescriptorSetBinding_Sampler)
uniform samplerCube inDiffuseEnvmap;

layout(rgba16f, set = 0, binding = kDescriptorSetBinding_StorageImageArray)
writeonly uniform imageCube outSpecularEnvmap[];

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(
  local_size_x = kCompute_Specular_kernelSize_x,
  local_size_y = kCompute_Specular_kernelSize_y,
  local_size_z = 1
) in;

void main() {
  const ivec3 coords = ivec3(gl_GlobalInvocationID.xyz);
  const int resolution = int(pushConstant.mapResolution);

  if (!all(lessThan(coords, ivec3(resolution, resolution, 6)))) {
    return;
  }

  // --------------

  const int numSamples = int(pushConstant.numSamples);
  const float inv_samples = 1.0f / float(numSamples);
  const uint mip_level = pushConstant.mipLevel;

  const float roughness_sqr = pushConstant.roughnessSquared;
  const float roughness = sqrt(roughness_sqr); //

  // When calculating the envmap, the Reflection ray is the View direction & the Normal.
  const vec3 N = cubemap_view_direction(coords, resolution);
  const vec3 V = N;

  // World-space basis from the view direction / normal.
  const mat3 basis_ws = basis_from_view(V);

  vec3 prefilteredColor = vec3(0.0);
  float totalWeight = 0.0f;

  for (int i = 0; i < numSamples; ++i) {
    const vec2 pt = hammersley2d( i, inv_samples);
    const vec3 H  =
#if 0
      importance_sample_GGX( basis_ws, pt, roughness)
#else
      fast_importance_sample_GGX( basis_ws, pt, roughness_sqr)
#endif
    ;
    const float v_dot_h = dot(V, H);
    const vec3 L = normalize(2.0 * v_dot_h * H - V);
    const float n_dot_l = max(dot(N, L), 0.0);

    if (n_dot_l > 0.0) {
      // Split sum approximation (Karis 2013) : the samples are already
      // distributed according to the GGX lobe, they only need to be weighted
      // by N.L (weighting by 1/pdf would cancel the importance sampling).
      prefilteredColor += texture(inDiffuseEnvmap, L).rgb * n_dot_l;
      totalWeight += n_dot_l;
    }
  }
  prefilteredColor /= max(totalWeight, 0.0001);

  // --------------

  imageStore(outSpecularEnvmap[mip_level], coords, vec4(prefilteredColor, 1.0));
}

// ----------------------------------------------------------------------------
