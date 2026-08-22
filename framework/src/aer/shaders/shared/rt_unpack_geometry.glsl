#ifndef SHADERS_SHARED_INC_GEOMETRY_GLSL_
#define SHADERS_SHARED_INC_GEOMETRY_GLSL_

// ----------------------------------------------------------------------------

// For GLSL this should be include *AFTER* the ObjBuffers_t buffer reference.

// ----------------------------------------------------------------------------

#include <material/interop.h> //

// ----------------------------------------------------------------------------

struct Triangle_t {
  Vertex v0;
  Vertex v1;
  Vertex v2;
};

Triangle_t unpack_triangle(
  uint64_t vertexAddr,
  uint64_t indexAddr,
  uint primitive_id
) {
#if defined(_GLSL_)
  Vertices vertices = Vertices(vertexAddr);
  Indices indices   = Indices(indexAddr);

  const uint base_index = 3 * primitive_id;
  const uint i0 = indices.u32[base_index + 0];
  const uint i1 = indices.u32[base_index + 1];
  const uint i2 = indices.u32[base_index + 2];

  Triangle_t tri;
  tri.v0 = vertices.v[i0];
  tri.v1 = vertices.v[i1];
  tri.v2 = vertices.v[i2];
#else
  let base_index = 3 * primitive_id;
  let i0 = *(uint32_t*)(indexAddr + base_index + 0);
  let i1 = *(uint32_t*)(indexAddr + base_index + 1);
  let i2 = *(uint32_t*)(indexAddr + base_index + 2);

  Triangle_t tri = Triangle_t(0);
  tri.v0 = *(Vertex*)(vertexAddr + i0);
  tri.v1 = *(Vertex*)(vertexAddr + i1);
  tri.v2 = *(Vertex*)(vertexAddr + i2);
#endif

  return tri;
}

// ----------------------------------------------------------------------------

vec3 barycenter_from_hit(in vec2 attribs) {
  return vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
}

// ----------------------------------------------------------------------------

vec3 calculate_local_position(in Triangle_t tri, in vec3 barycentrics) {
  vec3 P = tri.v0.position.xyz * barycentrics.x
         + tri.v1.position.xyz * barycentrics.y
         + tri.v2.position.xyz * barycentrics.z;
  return P;
}

vec3 calculate_world_normal(in Triangle_t tri, in vec3 barycentrics) {
  vec3 N = tri.v0.normal.xyz * barycentrics.x
         + tri.v1.normal.xyz * barycentrics.y
         + tri.v2.normal.xyz * barycentrics.z;
  return N;
}

vec2 calculate_texcoord(in Triangle_t tri, in vec3 barycentrics) {
  return tri.v0.texcoord * barycentrics.x
       + tri.v1.texcoord * barycentrics.y
       + tri.v2.texcoord * barycentrics.z;
}

// ----------------------------------------------------------------------------

Vertex calculate_vertex(
  in Triangle_t tri,
  in vec2 attribs,
  in mat4x3 worldMatrix,
  in mat4x3 invWorldMatrix
) {
  vec3 barycentrics = barycenter_from_hit(attribs);

  vec3 pos = calculate_local_position(tri, barycentrics);
  vec3 nor = calculate_world_normal(tri, barycentrics);

  Vertex v;

#if defined(_GLSL_)
  v.position = (worldMatrix * vec4(pos, 1.0f)).xyz;
  v.normal = normalize((nor * invWorldMatrix).xyz);
#else
  v.position = mul(vec4(pos, 1.0f), worldMatrix).xyz;
  v.normal = normalize(mul(vec4(nor, 0.0f), invWorldMatrix).xyz);
#endif

  v.texcoord  = calculate_texcoord(tri, barycentrics);

  return v;
}

// ----------------------------------------------------------------------------

#endif // SHADERS_SHARED_INC_GEOMETRY_GLSL_