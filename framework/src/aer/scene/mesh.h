#ifndef AER_SCENE_MESH_H_
#define AER_SCENE_MESH_H_

#include <vector>

#include "aer/scene/geometry.h"
#include "aer/scene/mesh_utils.h"

namespace scene {

/* -------------------------------------------------------------------------- */

struct Mesh : Geometry {
  // per primitive: index of the material in the owning Asset (file-local)
  std::vector<uint32_t> primitive_materials_index{}; //

 public:
  [[nodiscard]]
  VkIndexType vk_index_type() const noexcept {
    return ToVkIndexType(index_format());
  }

  [[nodiscard]]
  VkPrimitiveTopology vk_primitive_topology() const {
    return ToVkPrimitiveTopology(topology());
  }

  [[nodiscard]]
  VertexBufferDescriptors createPipelineVertexBufferDescriptors(
    Geometry::AttributeLocationMap const& attribute_to_location,
    uint32_t primitive_index = 0u
  ) const {
    return CreatePipelineVertexBufferDescriptors(
      *this, attribute_to_location, primitive_index
    );
  }
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_MESH_H_
