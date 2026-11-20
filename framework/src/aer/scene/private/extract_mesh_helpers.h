#ifndef AER_SCENE_PRIVATE_EXTRACT_MESH_HELPERS_H_
#define AER_SCENE_PRIVATE_EXTRACT_MESH_HELPERS_H_

#include <vector>

#include "aer/scene/geometry.h"
#include "aer/scene/mesh.h"
#include "aer/scene/skin_vertex.h"
#include "aer/scene/private/cgltf_wrapper.h"

// [LAYOUT] temporary: 'scene' should not depend on 'renderer' long term.
#include "aer/renderer/vertex_layout.h"

/* -------------------------------------------------------------------------- */

bool IsPrimitiveSupported(cgltf_primitive const& prim, bool restructure);

// ----------------------------------------------------------------------------

// Appends the primitive's (non-Draco) indices to the mesh.
// Returns false, WITHOUT touching the mesh, if the format is unsupported.
bool AppendPrimitiveIndices(
  cgltf_primitive const& prim,
  bool force32,
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
);

// ----------------------------------------------------------------------------

// Convert primitive attributes to VertexInternal_t + SkinVertex.
bool AppendPrimitiveRestructured(
  cgltf_primitive const& prim,
  bool force32,
  std::vector<VertexInternal_t>& vertices,   // scratch
  std::vector<scene::SkinVertex>& skin_vertices, // scratch
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
);

// ----------------------------------------------------------------------------

// Primitive attributes are copied as is, the layout of the first primitive is shared.
bool AppendPrimitiveRaw(
  cgltf_primitive const& prim,
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
);

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_PRIVATE_EXTRACT_MESH_HELPERS_H_