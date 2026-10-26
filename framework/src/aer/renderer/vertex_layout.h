#pragma once

#include "aer/core/common.h"
#include "aer/scene/geometry.h"
#include "aer/platform/vulkan/types.h"

namespace material_shader_interop {
#include "aer/shaders/material/interop.h"
}

/* -------------------------------------------------------------------------- */
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
/* -------------------------------------------------------------------------- */

//
// Internal interleaved vertex structure used when bRestructureAttribs is set to true.
// (Used for main topological attributes, others attributes should reside on different buffers,
//    eg. extra uvs, colors, skin weights, morph targets, ...)
//
struct VertexInternal_t : material_shader_interop::Vertex {
  // vec3 position; float _pad0[1];
  // vec3 normal;   float _pad1[1];
  // vec4 tangent;
  // vec2 texcoord; float _pad2[2];

  [[nodiscard]]
  static
  Geometry::AttributeInfoMap GetAttributeInfoMap() {
    return {
      {
        Geometry::AttributeType::Position,
        {
          .format = Geometry::AttributeFormat::RGB_F32,
          .offset = offsetof(VertexInternal_t, position),
          .stride = sizeof(VertexInternal_t),
        }
      },
      {
        Geometry::AttributeType::Normal,
        {
          .format = Geometry::AttributeFormat::RGB_F32,
          .offset = offsetof(VertexInternal_t, normal),
          .stride = sizeof(VertexInternal_t),
        }
      },
      {
        Geometry::AttributeType::Tangent,
        {
          .format = Geometry::AttributeFormat::RGBA_F32,
          .offset = offsetof(VertexInternal_t, tangent),
          .stride = sizeof(VertexInternal_t),
        }
      },
      {
        Geometry::AttributeType::Texcoord,
        {
          .format = Geometry::AttributeFormat::RG_F32,
          .offset = offsetof(VertexInternal_t, texcoord),
          .stride = sizeof(VertexInternal_t),
        }
      },
    };
  }

  [[nodiscard]]
  static
  Geometry::AttributeOffsetMap GetAttributeOffsetMap(uint64_t buffer_offset) {
    return {
      { Geometry::AttributeType::Position,  buffer_offset },
      { Geometry::AttributeType::Normal,    buffer_offset },
      { Geometry::AttributeType::Tangent,   buffer_offset },
      { Geometry::AttributeType::Texcoord,  buffer_offset },
    };
  }

  [[nodiscard]]
  static
  Geometry::AttributeLocationMap GetDefaultAttributeLocationMap() {
    return {
      { Geometry::AttributeType::Position, material_shader_interop::kAttribLocation_Position },
      { Geometry::AttributeType::Normal,   material_shader_interop::kAttribLocation_Normal },
      { Geometry::AttributeType::Tangent,  material_shader_interop::kAttribLocation_Tangent }, //
      { Geometry::AttributeType::Texcoord, material_shader_interop::kAttribLocation_Texcoord },
    };
  };

  [[nodiscard]]
  static
  VertexInputDescriptor GetVertexInputDescriptor(
    Geometry const& geo,
    uint64_t base_vertex_offset,
    uint64_t buffer_offset
  ) {
    return CreateVertexInputDescriptor(
      geo,
      base_vertex_offset,
      GetAttributeOffsetMap(buffer_offset),
      GetDefaultAttributeLocationMap()
    );
  }

  [[nodiscard]]
  static
  VertexBufferDescriptors GetPipelineVertexBufferDescriptors(
    Geometry const& geo,
    uint32_t primitive_index = 0u
  ) {
    return CreatePipelineVertexBufferDescriptors(
      geo, GetDefaultAttributeLocationMap(), primitive_index
    );
  }

 public:
  void applyTransform(mat4 const& matrix) {
    position = lina::to_vec3(lina::mul(matrix, lina::to_vec4(position, 1.0f)));
    auto const normalMatrix = lina::to_mat3(matrix);
    normal = lina::normalize(lina::mul(normalMatrix, normal));
    auto const t3 = lina::mul(normalMatrix, lina::to_vec3(tangent));
    tangent = vec4(lina::normalize(t3), tangent.w);
  }
};

/* -------------------------------------------------------------------------- */
