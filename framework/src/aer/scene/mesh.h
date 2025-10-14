#ifndef AER_SCENE_MESH_H_
#define AER_SCENE_MESH_H_

#include "aer/core/common.h"

#include "aer/scene/geometry.h"
#include "aer/platform/vulkan/types.h"      // for VertexInputDescriptor
#include "aer/renderer/pipeline.h"          // for PipelineVertexBufferDescriptors

namespace scene {

struct HostResources; //
struct MaterialRef;

/* -------------------------------------------------------------------------- */

struct Mesh : Geometry {
 public:
  struct SubMesh {
    Mesh const* parent{};
    DrawDescriptor draw_descriptor{};
    MaterialRef const* material_ref{};
  };

  struct BufferInfo {
    uint64_t vertex_offset{}; // (switch to array of binding_count size?)
    uint64_t index_offset{};
    uint64_t vertex_size{};
    uint64_t index_size{};
  };

 public:
  Mesh() = default;

  /* Bind mesh attributes to pipeline attributes location. */
  void initialize_submesh_descriptors(
    AttributeLocationMap const& attribute_to_location
  );

  /* Defines offset to actual data from external buffers. */
  void set_buffer_info(BufferInfo const& buffer_info) {
    buffer_info_ = {
      .vertex_offset = buffer_info.vertex_offset,
      .index_offset = buffer_info.index_offset,
      .vertex_size = get_vertices().size(),
      .index_size = get_indices().size(),
    };
  }

  /* Set pointer to global resources. */
  void set_resources_ptr(HostResources const* R);

  /* Return the mesh world transform. */
  mat4 const& world_matrix() const; //

 public:
  std::vector<SubMesh> submeshes{};
  uint32_t transform_index{};

 private:
  HostResources const* resources_ptr_{};
  BufferInfo buffer_info_{};

  /* ------- Renderer specifics ------- */

 public:
  PipelineVertexBufferDescriptors pipeline_vertex_buffer_descriptors() const;
  VkIndexType vk_index_type() const;
  VkPrimitiveTopology vk_primitive_topology() const;
  VkFormat vk_format(AttributeType const attrib_type) const;

 private:
  VertexInputDescriptor create_vertex_input_descriptors(
    AttributeOffsetMap const& attribute_to_offset,
    AttributeLocationMap const& attribute_to_location
  ) const;
};

/* -------------------------------------------------------------------------- */

}  // namespace scene

#endif // AER_SCENE_MESH_H_
