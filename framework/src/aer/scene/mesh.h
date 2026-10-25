#ifndef AER_SCENE_MESH_H_
#define AER_SCENE_MESH_H_

#include <vector>
#include "aer/scene/geometry.h"

namespace scene {

/* -------------------------------------------------------------------------- */

struct Mesh : Geometry {
  // per primitive: index of the material in the owning Asset (file-local)
  std::vector<uint32_t> primitive_materials_index{}; //
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_MESH_H_
