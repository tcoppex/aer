#include "aer/scene/mesh_utils.h"

/* -------------------------------------------------------------------------- */

VkFormat ToVkFormat(Geometry::AttributeFormat f) {
  switch (f) {
    case Geometry::AttributeFormat::RG_F32:   return VK_FORMAT_R32G32_SFLOAT;
    case Geometry::AttributeFormat::RGB_F32:  return VK_FORMAT_R32G32B32_SFLOAT;
    case Geometry::AttributeFormat::RGBA_F32: return VK_FORMAT_R32G32B32A32_SFLOAT;
    default:                                  return VK_FORMAT_UNDEFINED;
  }
}

// ----------------------------------------------------------------------------

VkIndexType ToVkIndexType(Geometry::IndexFormat f) {
  switch (f) {
    case Geometry::IndexFormat::U16: return VK_INDEX_TYPE_UINT16;
    case Geometry::IndexFormat::U8:  return VK_INDEX_TYPE_UINT8;
    default:                         return VK_INDEX_TYPE_UINT32;
  }
}

// ----------------------------------------------------------------------------

VkPrimitiveTopology ToVkPrimitiveTopology(Geometry::Topology t) {
  switch (t) {
    case Geometry::Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case Geometry::Topology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Geometry::Topology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    default:                                return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  }
}

/* -------------------------------------------------------------------------- */

VertexInputDescriptor CreateVertexInputDescriptor(
  Geometry const& geo,
  uint64_t base_vertex_offset,
  Geometry::AttributeOffsetMap const& attribute_to_offset,
  Geometry::AttributeLocationMap const& attribute_to_location
) {
  VertexInputDescriptor result{};
  std::map<uint64_t, std::vector<Geometry::AttributeType>> by_offset{};
  for (auto const& [type, offset] : attribute_to_offset) {
    by_offset[offset].push_back(type);
  }

  uint32_t binding = 0u;
  for (auto const& [offset, types] : by_offset) {
    bool exists = false;
    for (auto t : types) { exists |= attribute_to_location.contains(t); }
    if (!exists) { continue; }

    result.vertexBufferOffsets.push_back(base_vertex_offset + offset);
    result.bindings.push_back({
      .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
      .binding = binding,
      .stride = geo.attribute_stride(types[0]),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
      .divisor = 1u,
    });
    for (auto t : types) {
      if (auto it = attribute_to_location.find(t); it != attribute_to_location.end()) {
        result.attributes.push_back({
          .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
          .location = it->second,
          .binding = binding,
          .format = ToVkFormat(geo.attribute_format(t)),
          .offset = geo.attribute_offset(t),
        });
      }
    }
    ++binding;
  }
  return result;
}

/* -------------------------------------------------------------------------- */

VertexBufferDescriptors CreatePipelineVertexBufferDescriptors(
  Geometry const& geo,
  Geometry::AttributeLocationMap const& attribute_to_location,
  uint32_t primitive_index
) {
  if (primitive_index >= geo.primitive_count()) {
    LOGW("{}: primitive index out of range.", __FUNCTION__);
    return {};
  }
  auto const& prim = geo.primitive(primitive_index);

  std::map<uint64_t, std::vector<Geometry::AttributeType>> by_offset{};
  for (auto const& [type, offset] : prim.bufferOffsets) {
    if (attribute_to_location.contains(type) && geo.hasAttribute(type)) {
      by_offset[offset].push_back(type);
    }
  }

  VertexBufferDescriptors result{};
  for (auto const& [offset, types] : by_offset) {
    VertexBufferDescriptor buffer{
      .stride = geo.attribute_stride(types[0]),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };
    for (auto const type : types) {
      // (the binding index is filled by RenderContext::buildGraphicsPipelineCreateInfo)
      buffer.attributes.push_back({
        .location = attribute_to_location.at(type),
        .format = ToVkFormat(geo.attribute_format(type)),
        .offset = geo.attribute_offset(type),
      });
    }
    result.push_back(std::move(buffer));
  }
  return result;
}

/* -------------------------------------------------------------------------- */
