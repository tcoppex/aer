#ifndef SHADERS_SHARED_INC_RANDOM_GLSL_
#define SHADERS_SHARED_INC_RANDOM_GLSL_

#extension GL_GOOGLE_include_directive : require
#include <shared/constants.glsl>


// ----------------------------------------------------------------------------

// Generate a seed for the random generator.
// Input - pixel.x, pixel.y, frame_nb
// From https://github.com/Cyan4973/xxHash, https://www.shadertoy.com/view/XlGcRh
// (originally from NVPRO/vk_min_samples)
uint xxhash32(uvec3 p) {
  const uvec4 primes = uvec4(2246822519U, 3266489917U, 668265263U, 374761393U);
  uint        h32;
  h32 = p.z + primes.w + p.x * primes.y;
  h32 = primes.z * ((h32 << 17) | (h32 >> (32 - 17)));
  h32 += p.y * primes.y;
  h32 = primes.z * ((h32 << 17) | (h32 >> (32 - 17)));
  h32 = primes.x * (h32 ^ (h32 >> 15));
  h32 = primes.y * (h32 ^ (h32 >> 13));
  return h32 ^ (h32 >> 16);
}

// Tiny Encryption Alogrithm, alternative to seed the random generator.
// Input - pixel.x, pixel.y, frame_nb
uint tea(uvec3 p) {
  uint v0 = p.x + p.y * p.x;
  uint v1 = p.z;
  uint s0 = 0u;
  for (uint n = 0u; n < 16u; n++) {
    s0 += 0x9e3779b9u;
    v0 += ((v1 << 4) + 0xa341316cu) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4u);
    v1 += ((v0 << 4) + 0xad90777du) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761eu);
  }
  return v0;
}

// ----------------------------------------------------------------------------

// https://www.pcg-random.org/
uint pcg(inout uint state)
{
  uint prev = state * 747796405u + 2891336453u;
  uint word = ((prev >> ((prev >> 28u) + 4u)) ^ prev) * 277803737u;
  state     = prev;
  return (word >> 22u) ^ word;
}

// ----------------------------------------------------------------------------

#if 1
float randf(inout uint seed)
{
  uint r = pcg(seed);
  return float(r) * (1.F / float(0xffffffffu));
}
#else
float randf(inout uint seed) {
  seed ^= (seed << 13);
  seed ^= (seed >> 17);
  seed ^= (seed << 5);
  return float(seed & 0x00FFFFFFu) / float(0x01000000u);
}
#endif

vec2 rand2f(inout uint seed) {
  return vec2(randf(seed), randf(seed));
}

// ----------------------------------------------------------------------------

// Hash Functions for GPU Rendering, Jarzynski et al.
// Input - pixel.x, pixel.y, frame_nb + payload.depth
// http://www.jcgt.org/published/0009/03/02/
vec3 random_pcg3d(uvec3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y*v.z; v.y += v.z*v.x; v.z += v.x*v.y;
  v ^= v >> 16u;
  v.x += v.y*v.z; v.y += v.z*v.x; v.z += v.x*v.y;
  return vec3(v) * (1.0/float(0xffffffffu));
}

// ----------------------------------------------------------------------------

#endif // SHADERS_SHARED_INC_RANDOM_GLSL_
