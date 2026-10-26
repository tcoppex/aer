#include "aer/scene/private/extract_mesh_helpers.h"

#if defined(FRAMEWORK_HAS_DRACO) && VKPLAYGROUND_HAS_DRACO
#include <draco/compression/decode.h>
#include <draco/mesh/mesh.h>
static constexpr bool kFrameworkHasDraco{true};
#else
static constexpr bool kFrameworkHasDraco{false};
#endif

/* -------------------------------------------------------------------------- */

namespace {

bool DecompressDracoPrimitive(
  cgltf_primitive const& prim,
  std::vector<VertexInternal_t>& vertices,
  std::vector<uint32_t>& indices
) {
  if (!prim.has_draco_mesh_compression) {
    LOGE("Error: Primitive does not have draco compression.");
    return false;
  }

  cgltf_draco_mesh_compression const& draco = prim.draco_mesh_compression;
  if (!draco.buffer_view || !draco.buffer_view->buffer || !draco.buffer_view->buffer->data) {
    LOGE("Error: Draco buffer view is invalid.");
    return false;
  }

#if VKPLAYGROUND_HAS_DRACO
  std::unique_ptr<draco::Mesh> draco_mesh{};
  {
    cgltf_accessor const* pos_accessor = nullptr;

    for (cgltf_size attrib_index = 0; attrib_index < draco.attributes_count; ++attrib_index) {
      if (draco.attributes[attrib_index].type == cgltf_attribute_type_position) {
        pos_accessor = draco.attributes[attrib_index].data;
        break;
      }
    }
    if (!pos_accessor) {
      LOGE("Draco primitive has no POSITION attribute");
      return false;
    }

    cgltf_buffer_view const* view = draco.buffer_view; //pos_accessor->buffer_view;
    LOG_CHECK(view);

    cgltf_buffer const* buffer = view->buffer;
    uint8_t const* draco_data = reinterpret_cast<const uint8_t*>(buffer->data) + view->offset;
    size_t const draco_size = view->size;

    draco::DecoderBuffer decoder_buffer;
    decoder_buffer.Init(reinterpret_cast<const char*>(draco_data), draco_size);

    draco::Decoder decoder;
    if (auto result = decoder.DecodeMeshFromBuffer(&decoder_buffer); result.ok()) {
      draco_mesh = std::move(result).value();
    }
  }

  if (!draco_mesh) {
    LOGE("Draco decompression failed");
    return false;
  }

  uint32_t const vertex_count = draco_mesh->num_points();
  vertices.resize(vertex_count);

  for (cgltf_size attrib_index = 0; attrib_index < draco.attributes_count; ++attrib_index) {
    cgltf_attribute const& attrib = draco.attributes[attrib_index];

    draco::PointAttribute const* pt_attr = nullptr;

    switch (attrib.type) {
      case cgltf_attribute_type_position:
        pt_attr = draco_mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
        for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
          auto &vertex = vertices[vertex_index];
          pt_attr->GetValue(draco::AttributeValueIndex(vertex_index), lina::ptr(vertex.position));
        }
        break;

      case cgltf_attribute_type_normal:
        pt_attr = draco_mesh->GetNamedAttribute(draco::GeometryAttribute::NORMAL);
        for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
          auto &vertex = vertices[vertex_index];
          pt_attr->GetValue(draco::AttributeValueIndex(vertex_index), lina::ptr(vertex.normal));
        }
        break;

      case cgltf_attribute_type_texcoord:
        if (attrib.index > 0) {
          LOGW("MultiTexturing not supported (Draco)");
          continue;
        }
        pt_attr = draco_mesh->GetNamedAttribute(draco::GeometryAttribute::TEX_COORD);
        for (uint32_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
          auto &vertex = vertices[vertex_index];
          pt_attr->GetValue(draco::AttributeValueIndex(vertex_index), lina::ptr(vertex.texcoord));
        }
        break;

      default:
        LOGW("Unsupported Draco attribute type: {}", int(attrib.type));
        break;
    }
  }

  // Indices.
  if (prim.indices) {
    const int num_faces = draco_mesh->num_faces();
    indices.reserve(num_faces * 3);

    for (draco::FaceIndex face_index(0); face_index < num_faces; ++face_index) {
      const draco::Mesh::Face& face = draco_mesh->face(face_index);
      for (auto const& face_pt : face) {
        indices.push_back(face_pt.value());
      }
    }
  }
#endif

  return true;
}

void ExtractPrimitiveVertices(
  cgltf_primitive const& prim,
  std::vector<VertexInternal_t>& vertices
) {
  uint32_t const vertex_count = prim.attributes[0].data->count;
  vertices.resize(vertex_count);

  for (cgltf_size attrib_index = 0; attrib_index < prim.attributes_count; ++attrib_index) {
    cgltf_attribute const& attrib{ prim.attributes[attrib_index] };
    cgltf_accessor const* accessor = attrib.data;
    LOG_CHECK(accessor->count == vertex_count);

    // Positions.
    if (attrib.type == cgltf_attribute_type_position) {
      LOG_CHECK(accessor->type == cgltf_type_vec3);
      // LOGD( "> loading positions." );
      for (cgltf_size vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        auto& vertex = vertices[vertex_index];
        cgltf_accessor_read_float( accessor, vertex_index, lina::ptr(vertex.position), 3);
      }
    }
    // Normals.
    else if (attrib.type == cgltf_attribute_type_normal) {
      LOG_CHECK(accessor->type == cgltf_type_vec3);
      // LOGD( "> loading normals." );
      for (cgltf_size vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        auto& vertex = vertices[vertex_index];
        cgltf_accessor_read_float( accessor, vertex_index, lina::ptr(vertex.normal), 3);
      }
    }
    // Tangents
    else if (attrib.type == cgltf_attribute_type_tangent) {
      // LOG_CHECK(accessor->type == cgltf_type_vec4);
      // LOGD( "> loading tangents." );
      for (cgltf_size vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        auto& vertex = vertices[vertex_index];
        cgltf_accessor_read_float( accessor, vertex_index, lina::ptr(vertex.tangent), 4);
        // vec3 t3 = vec3(lina::mul(world_matrix, vec4(lina::to_vec3(tangent), 0.0f)));
        // vertex.tangent = vec4(t3, vertex.tangent.w);
      }
    }
    // Texcoords.
    else if (attrib.type == cgltf_attribute_type_texcoord) {
      LOG_CHECK(accessor->type == cgltf_type_vec2);
      if (attrib.index <= 0) {
        // LOGD( "> loading texture coordinates." );
        for (cgltf_size vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
          auto& vertex = vertices[vertex_index];
          cgltf_accessor_read_float( accessor, vertex_index, lina::ptr(vertex.texcoord), 2);
        }
      }
    }
    // Joints.
    else if (attrib.type == cgltf_attribute_type_joints) {
      // LOG_CHECK(accessor->type == cgltf_type_vec4);
      // LOGD( "> loading joint indices." );
      // for (cgltf_size vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
      //   LOG_CHECK(0 != cgltf_accessor_read_uint( accessor, vertex_index, lina::ptr(joints), 4));
      //   raw.joints.push_back( joints );
      // }
    }
    // Weights.
    else if (attrib.type == cgltf_attribute_type_weights) {
      // LOGD( "> loading joint weights." );
      // LOG_CHECK(accessor->type == cgltf_type_vec4);
      // raw.weights.reserve(vertex_count);
      // vec4 weights;
      // for (cgltf_size vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
      //   LOG_CHECK(0 != cgltf_accessor_read_float( accessor, vertex_index, lina::ptr(weights), 4));
      //   // weights = vec4(0.0, 0.0, 0.0, 0.0);
      //   raw.weights.push_back( weights );
      // }
    }
  }
}

} // namespace ""

/* -------------------------------------------------------------------------- */

bool IsPrimitiveSupported(cgltf_primitive const& prim, bool restructure) {
  if (prim.attributes_count == 0u) {
    LOGW("[GLTF] A primitive was missing attributes.");
    return false;
  }
  if (prim.has_draco_mesh_compression && (!kFrameworkHasDraco || !restructure)) {
    LOGW("[GLTF] Draco mesh compression is not supported.");
    return false;
  }
  // The restructured path only builds triangle lists.
  if (restructure ? (prim.type != cgltf_primitive_type_triangles)
                  : (ConvertTopology(prim) == Geometry::Topology::kUnknown)) {
    LOGW("[GLTF] Unsupported primitive type, skipped.");
    return false;
  }
  if (prim.targets_count > 0) {
    LOGW("[GLTF] Morph targets are not supported.");
  }
  for (cgltf_size k = 0; k < prim.attributes_count; ++k) {
    if (prim.attributes[k].data->is_sparse) {
      LOGW("[GLTF] Sparse attributes are not supported.");
      return false;
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

bool AppendPrimitiveIndices(
  cgltf_primitive const& prim,
  bool force32,
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
) {
  cgltf_accessor const* accessor = prim.indices;
  auto const format = ConvertIndexFormat(accessor);
  if (format == Geometry::IndexFormat::kUnknown) {
    LOGW("[GLTF] Unsupported index format.");
    return false;
  }

  cgltf_buffer_view const* view = accessor->buffer_view;
  size_t const index_size = cgltf_component_size(accessor->component_type);
  size_t const stride     = accessor->stride ? accessor->stride : index_size;

  auto const* src = reinterpret_cast<std::byte const*>(view->buffer->data)
                  + view->offset + accessor->offset;

  primitive.indexCount = static_cast<uint32_t>(accessor->count);

  if (force32 && format != Geometry::IndexFormat::U32) {
    std::vector<uint32_t> indices(accessor->count);
    for (size_t i = 0; i < accessor->count; ++i) {
      if (format == Geometry::IndexFormat::U16) {
        uint16_t v; std::memcpy(&v, src + i * stride, sizeof(v)); indices[i] = v;
      } else {
        indices[i] = static_cast<uint8_t>(src[i * stride]);
      }
    }
    mesh.set_index_format(Geometry::IndexFormat::U32);
    primitive.indexOffset = mesh.addIndicesData(std::as_bytes(std::span(indices)));
  } else {
    if (mesh.index_count() > 0 && mesh.index_format() != format) {
      LOGW("[GLTF] Primitives of a mesh use different index formats.");
    }
    mesh.set_index_format(format);
    primitive.indexOffset = mesh.addIndicesData(
      std::span(src, accessor->count * stride)
    );
  }
  return true;
}

// ----------------------------------------------------------------------------

bool AppendPrimitiveRestructured(
  cgltf_primitive const& prim,
  bool force32,
  std::vector<VertexInternal_t>& vertices,   // scratch
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
) {
  vertices.clear();

  if (prim.has_draco_mesh_compression) {
    std::vector<uint32_t> indices{};
    if (!DecompressDracoPrimitive(prim, vertices, indices)) {
      return false;
    }
    if (prim.indices) {
      mesh.set_index_format(Geometry::IndexFormat::U32);
      primitive.indexCount  = static_cast<uint32_t>(indices.size());
      primitive.indexOffset = mesh.addIndicesData(std::as_bytes(std::span(indices)));
    }
  } else {
    ExtractPrimitiveVertices(prim, vertices);
    if (prim.indices && !AppendPrimitiveIndices(prim, force32, mesh, primitive)) {
      return false;
    }
  }

  uint64_t const offset = mesh.addVerticesData(std::as_bytes(std::span(vertices)));
  primitive.vertexCount   = static_cast<uint32_t>(vertices.size());
  primitive.bufferOffsets = VertexInternal_t::GetAttributeOffsetMap(offset); //

  return true;
}

// ----------------------------------------------------------------------------

bool AppendPrimitiveRaw(
  cgltf_primitive const& prim,
  scene::Mesh& mesh,
  Geometry::Primitive& primitive
) {
  auto is_flat = [](cgltf_accessor const* acc) {
    constexpr size_t kAccessorOffsetLimit = 2048;
    return (acc->offset == 0) || (acc->offset >= kAccessorOffsetLimit);
  };

  if (mesh.primitive_count() == 0u) {
    mesh.set_topology(ConvertTopology(prim));
    for (cgltf_size j = 0; j < prim.attributes_count; ++j) {
      auto const& attribute = prim.attributes[j];
      auto const* accessor  = attribute.data;
      if (auto type = ConvertAttributeType(attribute);
          type != Geometry::AttributeType::kUnknown) {
        mesh.addAttribute(type, {
          .format = ConvertAttributeFormat(accessor),
          .offset = static_cast<uint32_t>(is_flat(accessor) ? 0u : accessor->offset),
          .stride = static_cast<uint32_t>(accessor->stride),
        });
      }
    }
  } else if (ConvertTopology(prim) != mesh.topology()) {
    LOGW("[GLTF] Primitives of a mesh must share the same topology, skipped.");
    return false;
  }

  if (prim.indices && !AppendPrimitiveIndices(prim, /*force32*/false, mesh, primitive)) {
    return false;
  }

  std::map<cgltf_accessor const*, uint64_t> accessor_offsets{};
  for (cgltf_size a = 0; a < prim.attributes_count; ++a) {
    auto const& attribute = prim.attributes[a];
    auto type = ConvertAttributeType(attribute);
    if (type == Geometry::AttributeType::kUnknown) {
      continue;
    }
    auto const* accessor = attribute.data;
    auto const* view     = accessor->buffer_view;
    primitive.vertexCount = static_cast<uint32_t>(accessor->count);

    uint64_t offset{};
    if (is_flat(accessor)) {
      auto const* base = reinterpret_cast<std::byte const*>(view->buffer->data)
                       + view->offset + accessor->offset;
      offset = mesh.addVerticesData(std::span(base, view->size - accessor->offset));
      accessor_offsets[accessor] = offset;
    } else {
      offset = accessor_offsets[accessor];
    }
    primitive.bufferOffsets[type] = offset;
  }
  return true;
}

/* -------------------------------------------------------------------------- */
