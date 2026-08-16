#ifndef SHADERS_SHARED_INC_GEOMETRY_GLSL_
#define SHADERS_SHARED_INC_GEOMETRY_GLSL_

// ----------------------------------------------------------------------------

// This should be include *AFTER* the ObjBuffers_t buffer reference.

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
  Vertices vertices = Vertices(vertexAddr);
  Indices indices   = Indices(indexAddr);

  // ----------

  const uint base_index = 3 * primitive_id;

  const uint i0 = indices.u32[base_index + 0];
  const uint i1 = indices.u32[base_index + 1];
  const uint i2 = indices.u32[base_index + 2];

  Vertex v0 = vertices.v[i0];
  Vertex v1 = vertices.v[i1];
  Vertex v2 = vertices.v[i2];

  Triangle_t tri;
  tri.v0 = v0;
  tri.v1 = v1;
  tri.v2 = v2;

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

vec3 calculate_world_position(in Triangle_t tri, in vec3 barycentrics) {
  vec3 P = calculate_local_position(tri, barycentrics);
  return vec3(gl_ObjectToWorldEXT * vec4(P, 1.0));
}

vec3 calculate_world_normal(in Triangle_t tri, in vec3 barycentrics) {
  vec3 N = tri.v0.normal.xyz * barycentrics.x
         + tri.v1.normal.xyz * barycentrics.y
         + tri.v2.normal.xyz * barycentrics.z;
  return N;
}

vec3 calculate_local_normal(in Triangle_t tri, in vec3 barycentrics) {
  vec4 N = calculate_world_normal(tri, barycentrics) * gl_WorldToObjectEXT;
  return normalize(vec3(N));
}

vec2 calculate_texcoord(in Triangle_t tri, in vec3 barycentrics) {
  return tri.v0.texcoord * barycentrics.x
       + tri.v1.texcoord * barycentrics.y
       + tri.v2.texcoord * barycentrics.z;
}

// ----------------------------------------------------------------------------

Vertex calculate_vertex(in Triangle_t tri, in vec2 attribs) {
  vec3 barycentrics = barycenter_from_hit(attribs);

  Vertex v;
  v.position  = calculate_world_position(tri, barycentrics);
  v.normal    = calculate_local_normal(tri, barycentrics);
  v.texcoord  = calculate_texcoord(tri, barycentrics);
  return v;
}

// ----------------------------------------------------------------------------

#endif // SHADERS_SHARED_INC_GEOMETRY_GLSL_