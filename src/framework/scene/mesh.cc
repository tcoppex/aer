#include "framework/scene/mesh.h"
#include "framework/scene/resources.h" //

/* -------------------------------------------------------------------------- */

namespace scene {

void Mesh::initialize_submesh_descriptors(AttributeLocationMap const& attribute_to_location) {
  LOG_CHECK( get_primitive_count() > 0 );
  if (submeshes.empty()) {
    submeshes.resize(get_primitive_count(), {.parent = this});
  }
  LOG_CHECK( get_primitive_count() == submeshes.size() );

  for (uint32_t i = 0u; i < get_primitive_count(); ++i) {
    auto const& prim{ get_primitive(i) };
    auto& submesh{ submeshes[i] };

    submesh.draw_descriptor = {
      .vertexInput = create_vertex_input_descriptors(prim.bufferOffsets, attribute_to_location),
      .indexOffset = device_buffer_info_.index_offset + prim.indexOffset, //
      .indexType = vk_index_type(),
      .indexCount = prim.indexCount,
      .vertexCount = prim.vertexCount,
      .instanceCount = 1u, //
    };
  }
}

// ----------------------------------------------------------------------------

PipelineVertexBufferDescriptors Mesh::vk_pipeline_vertex_buffer_descriptors() const {
  assert( !submeshes.empty() );
  auto const& vi{ submeshes[0u].draw_descriptor.vertexInput };

  PipelineVertexBufferDescriptors result(vi.bindings.size());

  std::transform(
    vi.bindings.cbegin(),
    vi.bindings.cend(),
    result.begin(),
    [&vi](auto const& binding) -> PipelineVertexBufferDescriptor {
      std::vector<VkVertexInputAttributeDescription> attribs{};
      for (auto const& attrib : vi.attributes) {
        if (attrib.binding == binding.binding) {
          attribs.push_back({
            .location = attrib.location,
            .binding = attrib.binding,
            .format = attrib.format,
            .offset = attrib.offset,
          });
        }
      }
      return {
        .stride = binding.stride,
        .inputRate = binding.inputRate,
        .attributes = attribs,
      };
    }
  );

  return result;
}

// ----------------------------------------------------------------------------

VkPrimitiveTopology Mesh::vk_primitive_topology() const {
  switch (get_topology()) {
    case Topology::TriangleStrip:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

    case Topology::TriangleList:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    default:
    case Topology::PointList:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  }
}

// ----------------------------------------------------------------------------

VkIndexType Mesh::vk_index_type() const {
  auto format = get_index_format();
  switch (format) {

    case IndexFormat::U32:
      return VK_INDEX_TYPE_UINT32;

    case IndexFormat::U16:
      return VK_INDEX_TYPE_UINT16;

    // VULKAN 1.4 or VK_KHR_index_type_uint8
    case IndexFormat::U8:
      return VK_INDEX_TYPE_UINT8;

    default:
      LOGD("Unsupported IndexFormat : %d", int(format));
      return VK_INDEX_TYPE_UINT8;
  }
}

// ----------------------------------------------------------------------------

VkFormat Mesh::vk_format(AttributeType const attrib_type) const {
  switch (get_format(attrib_type)) {
    case AttributeFormat::RG_F32:
      return VK_FORMAT_R32G32_SFLOAT;
    break;

    case AttributeFormat::RGB_F32:
      return VK_FORMAT_R32G32B32_SFLOAT;
    break;

    case AttributeFormat::RGBA_F32:
      return VK_FORMAT_R32G32B32A32_SFLOAT;
    break;

    default:
      return VK_FORMAT_UNDEFINED;
  }
}

// ----------------------------------------------------------------------------

VertexInputDescriptor Mesh::create_vertex_input_descriptors(AttributeOffsetMap const& attribute_to_offset, AttributeLocationMap const& attribute_to_location) const {
  VertexInputDescriptor result{};

  std::map<uint64_t, std::vector<AttributeType>> offset_to_attributes{};
  for (auto const& [attrib_type, attrib_offset] : attribute_to_offset) {
    offset_to_attributes[attrib_offset].push_back(attrib_type);
  }

  uint32_t buffer_binding = 0u;

  for (auto const& [attrib_offset, attrib_types] : offset_to_attributes) {
    /* Be sure any of the attributes asked for exist. */
    bool exists = false;
    for (auto const& type : attrib_types) {
      exists |= attribute_to_location.contains(type);
    }
    if (!exists) {
      continue;
    }

    /* The stride is shared between attributes of the same binding. */
    uint32_t const buffer_stride = get_stride(attrib_types[0u]);
    uint64_t const buffer_offset = device_buffer_info_.vertex_offset + attrib_offset;

    result.vertexBufferOffsets.push_back(buffer_offset);

    result.bindings.push_back({
      .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
      .binding = buffer_binding,
      .stride = buffer_stride,
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX, //
      .divisor = 1u,
    });

    for (auto const& type : attrib_types) {
      if (auto it = attribute_to_location.find(type); it != attribute_to_location.end()) {
        result.attributes.push_back({
          .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
          .location = it->second,
          .binding = buffer_binding,
          .format = vk_format(type),
          .offset = get_offset(type),
        });
      }
    }
    ++buffer_binding;
  }

  return result;
}

// ----------------------------------------------------------------------------

void Mesh::set_resources_ptr(Resources const* R) {
  resources_ptr = R;
}

// ----------------------------------------------------------------------------

mat4 const& Mesh::world_matrix() const {
  LOG_CHECK(resources_ptr != nullptr);
  return resources_ptr->transforms[transform_index];
}

} // namespace "scene"

/* -------------------------------------------------------------------------- */
