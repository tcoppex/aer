#ifndef SHADERS_SHARED_INC_MATHS_GLSL_
#define SHADERS_SHARED_INC_MATHS_GLSL_

#include <shared/constants.glsl>

// ----------------------------------------------------------------------------

float cbrt(float x) { 
  return pow(x, 0.33333);
}

// ----------------------------------------------------------------------------

mat3 rotationX(float c, float s) {
  return mat3(
    vec3(1, 0, 0),
    vec3(0, c, s),
    vec3(0, -s, c)
  );
}

mat3 rotationY(float c, float s) {
  return mat3(
    vec3(c, 0, -s),
    vec3(0, 1, 0),
    vec3(s, 0, c)
  );
}

mat3 rotationZ(float c, float s) {
  return mat3(
    vec3(c, s, 0),
    vec3(-s, c, 0),
    vec3(0, 0, 1)
  );
}

// ----------------------------------------------------------------------------

mat3 rotationX(float radians) {
  return rotationX(cos(radians), sin(radians));
}

mat3 rotationY(float radians) {
  return rotationY(cos(radians), sin(radians));
}

mat3 rotationZ(float radians) {
  return rotationZ(cos(radians), sin(radians));
}

// ----------------------------------------------------------------------------

mat3 calculate_reorient_matrix(vec3 u, vec3 v) {
  u = normalize(u);
  v = normalize(v);

  // Handle opposite directions.
  const float dp = dot(u, v);
  if (dp < -0.9999)
  {
    // Pick any orthogonal vector.
    const vec3 ortho = abs(u.x) > 0.1 ? vec3(0,1,0) : vec3(1,0,0);
    const vec3 axis = normalize(cross(u, ortho));
    float x = axis.x, y = axis.y, z = axis.z;
    return mat3(
      -1.0 + 2.0*x*x,  2.0*x*y,        2.0*x*z,
       2.0*x*y,       -1.0 + 2.0*y*y,  2.0*y*z,
       2.0*x*z,        2.0*y*z,       -1.0 + 2.0*z*z
    );
  }

  // Rodrigues' rotation formula.
  const vec3 c = cross(u, v);
  const mat3 M = mat3(
      0,   -c.z,    c.y,
    c.z,      0,   -c.x,
   -c.y,    c.x,      0
  );
  return mat3(1.0) + M + M * M * ((1.0 - dp) / dot(c, c));
}

// ----------------------------------------------------------------------------

mat3 basis_from_view(in vec3 z_axis) {
  // Check Y & Z are not colinears.
  vec3 y_axis = abs(z_axis.y) < (1.0 - Epsilon()) ? vec3(0.0, 1.0, 0.0)
                                                  : vec3(1.0, 0.0, 0.0)
                                                  ;
  vec3 x_axis = normalize(cross(y_axis, z_axis));
       y_axis = normalize(cross(z_axis, x_axis));
       z_axis = normalize(z_axis);
  return mat3(x_axis, y_axis, z_axis);
}

void build_tangent_basis(in vec3 N, out vec3 T, out vec3 B) {
  if (abs(N.z) < 0.999) {
    T = normalize(cross(N, vec3(0,0,1)));
  } else {
    T = normalize(cross(N, vec3(0,1,0)));
  }
  B = cross(N, T);
}

// ----------------------------------------------------------------------------

/* Return a view direction from uv coordinates and a cubemap face id. */
vec3 cubemap_view_direction(float u, float v, int faceId)  {
  u = 2.0 * u - 1.0f;
  v = 2.0 * v - 1.0f;
  const vec3 texeldirs[6] = {
    vec3( +1.0f, -v, -u),  // +X
    vec3( -1.0f, -v, +u),  // -X
    vec3( +u, +1.0f, +v),  // +Y
    vec3( +u, -1.0f, -v),  // -Y
    vec3( +u, -v, +1.0f),  // +Z
    vec3( -u, -v, -1.0f),  // -Z
  };
  return normalize( texeldirs[faceId] );
}

/* Return a view direction from a compute kernel coordinates, coords.z is the face id. */
vec3 cubemap_view_direction(in ivec3 coords, int resolution) {
  const float texelSize = 1.0f / float(resolution);
  const float u = (float(coords.x) + 0.5f) * texelSize;
  const float v = (float(coords.y) + 0.5f) * texelSize;
  return cubemap_view_direction(u, v, coords.z);
}

// ----------------------------------------------------------------------------

/* Return spherical coordinates from a cubemap view direction. */
vec2 cubemap_to_spherical_coords(in vec3 view) {
  const vec2 kInvPi = vec2(0.15915494, 0.318309886);
  const vec2 uv = vec2(atan(-view.z, view.x), asin(-view.y));
  return fma(uv, kInvPi, vec2(0.5));
}

// ----------------------------------------------------------------------------

vec2 trig_angle(float radians) {
  return vec2( cos(radians), sin(radians));
}

vec2 update_trig_angle(in vec2 base, in vec2 delta, float s) {
  return vec2(
    base.x * delta.x - s * base.y * delta.y,  // cos(a +/- b) = cos(a)*cos(b) -/+ sin(a)*sin(b)
    base.y * delta.x + s * base.x * delta.y   // sin(a +/- b) = sin(a)*cos(b) +/- cos(a)*sin(b) 
  );
}

vec2 inc_trig_angle(in vec2 base, in vec2 delta) {
  return update_trig_angle(base, delta, +1.0);
}

vec2 dec_trig_angle(in vec2 base, in vec2 delta) {
  return update_trig_angle(base, delta, -1.0);
}

// ----------------------------------------------------------------------------

float triangle_area(in vec3 A, in vec3 B, in vec3 C) {
  const vec3 AB = B - A;
  const vec3 AC = C - A;
  const float lenCP = sqrt(dot(AC, AC) - dot(AC, AB));
  return 0.5 * lenCP * length(AB);
}

// ----------------------------------------------------------------------------

// Generate stochastic coordinates in a triangle.
// ref. Greg Turk, "Graphics Gems", Academic Press, 1990
vec4 sample_triangle(in vec4 p0, in vec4 p1, in vec4 p2, vec2 st) {
  st.y = sqrt(st.y);
  vec3 coords;
  coords.x = 1.0 - st.y;
  coords.y = (1.0 - st.x) * st.y;
  coords.z = st.x * st.y;
  return mat3x4(p0, p1, p2) * coords;
}

// faster alternative.
vec4 sample_triangle2(in vec4 p0, in vec4 p1, in vec4 p2, vec2 st) {
  if ((st.x + st.y) > 1.0) {
    st.x = max(st.x, st.y);
    st.y = min(st.x, st.y);
    st.x = 1.0 - st.x;
  }
  vec3 coords = vec3(st.xy, 1.0 - (st.x + st.y));
  return mat3x4(p0, p1, p2) * coords;
}

// ----------------------------------------------------------------------------

// Sample a disk from 2 stochastic values.
vec3 sample_disk(float radius, vec2 rn) {
  const float r = radius * rn.x;
  const float theta = TwoPi() * rn.y;
  return vec3(r * cos(theta), 0.0, r * sin(theta));
}

// ----------------------------------------------------------------------------

// Sample a disk evenly for a given samples size.
// ref : http://blog.marmakoide.org/?p=1
vec3 sample_disk_even(float radius, uint id, uint total) {
  const float theta = id * GoldenAngle();
  const float r = radius *  sqrt(id / float(total));
  return r * vec3(cos(theta), 0, sin(theta));
}

// ----------------------------------------------------------------------------

// Sample a ball (interior & surface of a sphere) from 3 stochastic values.
// ref : so@5408276
vec3 sample_sphere_in(float radius, vec3 rn) {
  const float costheta = 2.0 * rn.x - 1.0;
  const float phi = TwoPi() * rn.y;
  const float theta = acos(costheta);
  const float r = radius * cbrt(rn.z);
  const float s = sin(theta);
  return r * vec3(s * cos(phi), s * sin(phi), costheta);
}

// ----------------------------------------------------------------------------

// Sample a sphere surface from 2 stochastic values.
// ref : https://www.cs.cmu.edu/~mws/rpos.html
//       https://gist.github.com/dinob0t/9597525 
vec3 sample_sphere_out(float radius, vec2 rn) {
  const float theta = TwoPi() * rn.x;
  const float z = radius * (2.0 * rn.y - 1.0);
  const float r = sqrt(radius * radius - z * z);
  return vec3(r * cos(theta), r * sin(theta), z);
}

// ----------------------------------------------------------------------------

// Samples points on an hemisphere using the Hammersley point set.
// ref : https://web.archive.org/web/20240707031450/ +
//       http://holger.dammertz.org/stuff/notes_HammersleyOnHemisphere.html
float radicalInverse_VdC(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

vec2 hammersley2d(int i, float inv_N) {
  return vec2(float(i+0.5) * inv_N, radicalInverse_VdC(uint(i+1)));
}

vec3 sample_hemisphere_uniform(vec2 u) {
 const float cosTheta = 1.0 - u.x;
 const float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
 const float phi = TwoPi() * u.y;
 return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 sample_hemisphere_cosine(vec2 u) {
  float r = sqrt(u.x);
  float theta = TwoPi() * u.y;
  return vec3(r * cos(theta), r * sin(theta), sqrt(max(0.0, 1.0 - u.x)));
}

// ----------------------------------------------------------------------------

// Sample an hemisphere using Hammersley pointset on a roughness dependant specular lobe
// and return it as a direction vector in world space.
// [potentially incorrect, use roughness squared instead of roughness]
vec3 fast_importance_sample_GGX(in mat3 basis_ws, in vec2 pt, float roughness_sqr) {
  const float a = roughness_sqr * roughness_sqr * pt.y; //
  const float u = a / (1.0 + a - pt.y);
  const float v = pt.x;
  return normalize(basis_ws * sample_hemisphere_cosine(vec2(u, v)));
}

// [flatten up version]
vec3 importance_sample_GGX(in mat3 basis_ws, in vec2 xi, float roughness) {
  float a = roughness * roughness;
  float phi = TwoPi() * xi.x;
  float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
  float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
  vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
  return normalize(basis_ws * H);
}

// ----------------------------------------------------------------------------

vec4 hermite_mix(in vec3 p0, in vec3 p1, in vec3 t0, in vec3 t1, in float u) {
  const mat4 mHermit = mat4(
    vec4( 2.0, -3.0, 0.0, 1.0),
    vec4(-2.0,  3.0, 0.0, 0.0),
    vec4( 1.0, -2.0, 1.0, 0.0),
    vec4( 1.0, -1.0, 0.0, 0.0)
  );  
  
  vec4 vU = vec4(u*u*u, u*u, u, 1.0);

  mat4 B = mat4( 
    vec4(p0.x, p1.x, t0.x, t1.x),
    vec4(p0.y, p1.y, t0.y, t1.y),
    vec4(p0.z, p1.z, t0.z, t1.z),
    vec4(1.0, 1.0, 0.0, 0.0)
  );

  return vU * mHermit * B;
}

// ----------------------------------------------------------------------------

float saturate(float x) {
  return min(max(x, 0.0), 1.0);
}

vec2 saturate(vec2 x) {
  return min(max(x, 0.0), 1.0);
}

// ----------------------------------------------------------------------------

// Map a range from [edge0, edge1] to [0, 1].
float maprange(float edge0, float edge1, float x) {
  return clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
}

// ----------------------------------------------------------------------------

// Map a value to smoothly peak at edge in [0, 1].
float smoothcurve(float edge, float x) {
  return smoothstep(0, edge, x) - smoothstep(edge, 1, x);
}

// ----------------------------------------------------------------------------

// float curve_inout(in float x, in float edge) {
//   const float a = maprange(0.0, edge, x);
//   const float b = maprange(edge, 1.0, x);
//   const float easein  = a * (2.0 - a);         // a * a;
//   const float easeout = b*b - 2.0 * b + 1.0;  // 1.0 - b * b;
//   return mix(easein, easeout, step(edge, x));
// }

// ----------------------------------------------------------------------------

// Higher order smoothstep.
float smoothstep2(float edge0, float edge1, float x) {
  const float t = maprange(edge0, edge1, x);
  return t * t * t * (10.0 + t *(-15.0 + 6.0 * t));
}

// ----------------------------------------------------------------------------

float gaussian(float sigma, float x_mu) {
  return exp(-(x_mu * x_mu) / (2.0 * sigma * sigma)) / (2.5066282 * abs(sigma));
}

vec3 gaussian(vec3 sigma, vec3 x_mu) {
  return exp(-(x_mu * x_mu) / (2.0 * sigma * sigma)) / (2.5066282 * abs(sigma));
}

// ----------------------------------------------------------------------------

// Normal Encoding / Decoding.
// ref: https://aras-p.info/texts/CompactNormalStorage.html

vec2 encodeNormal(vec3 n) {
  float p = sqrt(n.z * 8.0 + 8.0);
  return vec2(n.xy / p + 0.5);
}

vec3 decodeNormal(vec2 encoded) {
  vec2 f_encoded = 4.0 * encoded - 2.0;
  float f = dot(f_encoded, f_encoded);
  float g = sqrt(1 - f / 4);
  vec3 n;
  n.xy = f_encoded * g;
  n.z  = 1.0 - 0.5 * f;
  return normalize(n);
}

// ----------------------------------------------------------------------------

#endif // SHADERS_SHARED_INC_MATHS_GLSL_