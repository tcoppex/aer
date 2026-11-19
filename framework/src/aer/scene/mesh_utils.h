#ifndef AER_SCENE_MESH_UTILS_H_
#define AER_SCENE_MESH_UTILS_H_

#include "aer/core/common.h"

#include "aer/scene/geometry.h"
#include "aer/platform/vulkan/types.h"

/* -------------------------------------------------------------------------- */

[[nodiscard]]
VkFormat ToVkFormat(Geometry::AttributeFormat f);

[[nodiscard]]
VkIndexType ToVkIndexType(Geometry::IndexFormat f);

[[nodiscard]]
VkPrimitiveTopology ToVkPrimitiveTopology(Geometry::Topology t);

[[nodiscard]]
VertexInputDescriptor CreateVertexInputDescriptor(
  Geometry const& geo,
  uint64_t base_vertex_offset,
  Geometry::AttributeOffsetMap const& attribute_to_offset,
  Geometry::AttributeLocationMap const& attribute_to_location
);

/* Static pipeline vertex input for a geometry primitive: one binding per distinct
 * buffer offset (attributes sharing an offset are interleaved). Bindings come in
 * ascending offset order, the same order as CreateVertexInputDescriptor. */
[[nodiscard]]
VertexBufferDescriptors CreatePipelineVertexBufferDescriptors(
  Geometry const& geo,
  Geometry::AttributeLocationMap const& attribute_to_location,
  uint32_t primitive_index = 0u
);

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_MESH_UTILS_H_