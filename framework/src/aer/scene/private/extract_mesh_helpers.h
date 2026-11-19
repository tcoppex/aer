#ifndef AER_SCENE_PRIVATE_EXTRACT_MESH_HELPERS_H_
#define AER_SCENE_PRIVATE_EXTRACT_MESH_HELPERS_H_

#include <vector>

#include "aer/scene/geometry.h"
#include "aer/scene/mesh.h"

// [LAYOUT] temporary: 'scene' should not depend on 'renderer' long term.
#include "aer/renderer/vertex_layout.h"

#include "aer/scene/private/cgltf_wrapper.h"

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

// Restructured path: every primitive is converted to VertexInternal_t.
bool AppendPrimitiveRestructured(
  cgltf_primitive const& prim,
  bool force32,
  std::vector<VertexInternal_t>& vertices,   // scratch
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
);

// ----------------------------------------------------------------------------

// Raw path: attributes are copied as is, the layout of the first primitive is shared.
bool AppendPrimitiveRaw(
  cgltf_primitive const& prim,
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
);

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_PRIVATE_EXTRACT_MESH_HELPERS_H_