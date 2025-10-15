#pragma once

// ----------------------------------------------------------------------------

#include "aer/core/common.h"
#include "aer/scene/geometry.h"

namespace material_shader_interop {
#include "aer/shaders/material/interop.h"
}

// ----------------------------------------------------------------------------

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

  static
  Geometry::AttributeOffsetMap GetAttributeOffsetMap(uint64_t buffer_offset) {
    return {
      { Geometry::AttributeType::Position,  buffer_offset },
      { Geometry::AttributeType::Normal,    buffer_offset },
      { Geometry::AttributeType::Tangent,   buffer_offset },
      { Geometry::AttributeType::Texcoord,  buffer_offset },
    };
  }

  static
  Geometry::AttributeLocationMap GetDefaultAttributeLocationMap() {
    return {
      { Geometry::AttributeType::Position, material_shader_interop::kAttribLocation_Position },
      { Geometry::AttributeType::Normal,   material_shader_interop::kAttribLocation_Normal },
      { Geometry::AttributeType::Tangent,  material_shader_interop::kAttribLocation_Tangent }, //
      { Geometry::AttributeType::Texcoord, material_shader_interop::kAttribLocation_Texcoord },
    };
  };
};

// ----------------------------------------------------------------------------
