module rt_geometry;

#include <material/interop.h> //

namespace rt_geometry {

// ----------------------------------------------------------------------------

public
struct Triangle_t {
  public Vertex v0;
  public Vertex v1;
  public Vertex v2;
};

public
Triangle_t unpack_triangle(
  RTGeometryData geometry,
  uint primitive_id
) {
  Vertex* vertices = (Vertex*)geometry.vertex_address;

  let base_index = 3 * primitive_id;
  uint i0, i1, i2;

  if (geometry.index_is_u16 != 0u) {
    uint16_t* indices = (uint16_t*)geometry.index_address;
    i0 = uint(indices[base_index + 0]);
    i1 = uint(indices[base_index + 1]);
    i2 = uint(indices[base_index + 2]);
  } else {
    uint32_t* indices = (uint32_t*)geometry.index_address;
    i0 = indices[base_index + 0];
    i1 = indices[base_index + 1];
    i2 = indices[base_index + 2];
  }

  Triangle_t tri;
  tri.v0 = vertices[i0];
  tri.v1 = vertices[i1];
  tri.v2 = vertices[i2];
  return tri;
}

// ----------------------------------------------------------------------------

public
RTGeometryData fetch_geometry(uint64_t table_address, uint index) {
  return ((RTGeometryData*)table_address)[index];
}

// ----------------------------------------------------------------------------

public
float3 barycenter_from_hit(in float2 attribs) {
  return float3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
}

public
float3 calculate_local_position(in Triangle_t tri, in float3 barycentrics) {
  return tri.v0.position.xyz * barycentrics.x
       + tri.v1.position.xyz * barycentrics.y
       + tri.v2.position.xyz * barycentrics.z;
}

public
float3 calculate_world_normal(in Triangle_t tri, in float3 barycentrics) {
  return tri.v0.normal.xyz * barycentrics.x
       + tri.v1.normal.xyz * barycentrics.y
       + tri.v2.normal.xyz * barycentrics.z;
}

public
float2 calculate_texcoord(in Triangle_t tri, in float3 barycentrics) {
  return tri.v0.texcoord * barycentrics.x
       + tri.v1.texcoord * barycentrics.y
       + tri.v2.texcoord * barycentrics.z;
}

// ----------------------------------------------------------------------------

public
Vertex calculate_vertex(
  in Triangle_t tri,
  in float2 attribs,
  in float3x4 worldMatrix,
  in float3x4 invWorldMatrix
) {
  let barycentrics = barycenter_from_hit(attribs);
  let pos = calculate_local_position(tri, barycentrics);
  let nor = calculate_world_normal(tri, barycentrics);

  Vertex v;
  v.position = mul(worldMatrix, float4(pos, 1.0f)).xyz;
  v.normal   = normalize(mul(nor, invWorldMatrix).xyz);
  v.texcoord = calculate_texcoord(tri, barycentrics);

  return v;
}

} // namespace "rt_geometry"