#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require

// ----------------------------------------------------------------------------
//
// Calculate the 2D look-up table for the environment BRDF.
//  X is the cosine of the view angle, ie. N dot V.
//  Y is the roughness of the material.
//
// This could be further improved by dividing samples by threads (in grid Z).
//
// Reference :
//  * "Real Shading in Unreal Engine 4" - Brian Karis
//
// ----------------------------------------------------------------------------

#include <skybox/interop.h>
#include <shared/maths.glsl>        // for importance_sample_GGX
#include <shared/lighting/pbr.glsl> // for gf_SmithGGX

// ----------------------------------------------------------------------------

layout(rg16f, set = 0, binding = kDescriptorSetBinding_IntegrateBRDF_StorageImage) //
uniform writeonly image2D outImage[];

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

vec2 integrate_brdf(float n_dot_v, float roughness, int numSamples) {
  vec2 brdf = vec2(0.0);

  const float inv_samples   = 1.0f / float(numSamples);
  const float roughness_sqr = pow( roughness, 2.0);
  const mat3 basis_ws       = basis_from_view(vec3(0.0, 0.0, 1.0)); //

  const vec3 V = vec3(sqrt(1.0 - pow(n_dot_v, 2.0)), 0.0, n_dot_v);

  for (int i = 0; i < numSamples; ++i) {
    const vec2 pt = hammersley2d( i, inv_samples);

    const vec3 H  =
#if 0
      importance_sample_GGX( basis_ws, pt, roughness)
#else
      fast_importance_sample_GGX( basis_ws, pt, roughness_sqr)
#endif
    ;

    float v_dot_h = dot(V, H);
    const vec3 L  = /*normalize*/(2.0 * v_dot_h * H - V);

    const float n_dot_l = saturate(L.z);

    if (n_dot_l > 0.0) {
      const float n_dot_h = saturate(H.z);
      v_dot_h = saturate(v_dot_h);

      const float G     = gf_SmithGGX( n_dot_v, n_dot_l, roughness_sqr);
      const float G_Vis = G * v_dot_h / (n_dot_h * n_dot_v);
      const float Fc    = pow( 1.0 - v_dot_h, 5.0);

      brdf += vec2(1.0 - Fc, Fc) * G_Vis;
    }
  }
  brdf *= inv_samples;

  return brdf;
}

// ----------------------------------------------------------------------------

layout(
  local_size_x = kCompute_IntegrateBRDF_kernelSize_x,
  local_size_y = kCompute_IntegrateBRDF_kernelSize_y
) in;

void main()
{
  const ivec2 coords = ivec2(gl_GlobalInvocationID.xy);

  const int resolution = int(pushConstant.mapResolution);
  const int numSamples = int(pushConstant.numSamples);

  if (!all(lessThan(coords, ivec2(resolution)))) {
    return;
  }

  // Sample at texel centers : (n_dot_v = 0) would divide by zero in the integral,
  // and the lookup is done with a bilinear sampler.
  const vec2 uv = (vec2(coords.xy) + 0.5) / float(resolution);
  const vec2 data = integrate_brdf(uv.x, uv.y, numSamples);

  imageStore(outImage[0], coords, vec4(data, 0.0, 1.0));
}

// ----------------------------------------------------------------------------
