#define CGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include <string>

#include "aer/scene/private/gltf_loader.h"
#include "aer/scene/vertex_internal.h"

#if defined(FRAMEWORK_HAS_DRACO) && VKPLAYGROUND_HAS_DRACO
#include <draco/compression/decode.h>
#include <draco/mesh/mesh.h>
static constexpr bool kFrameworkHasDraco{true};
#else
static constexpr bool kFrameworkHasDraco{false};
#endif

/* -------------------------------------------------------------------------- */

namespace {

scene::MaterialModel GetMaterialModel(cgltf_material const& mat) {
  if (mat.unlit) {
    return scene::MaterialModel::Unlit;
  } else if (mat.has_pbr_metallic_roughness) {
    return scene::MaterialModel::PBRMetallicRoughness;
  }
  return scene::MaterialModel::Unknown;
}

// ----------------------------------------------------------------------------

scene::MaterialStates GetMaterialStates(cgltf_material const& mat) {
  scene::MaterialStates states{
    .alpha_mode = (mat.alpha_mode == cgltf_alpha_mode_opaque) ?
        scene::MaterialStates::AlphaMode::Opaque
      : (mat.alpha_mode == cgltf_alpha_mode_blend) ?
        scene::MaterialStates::AlphaMode::Blend
      : scene::MaterialStates::AlphaMode::Mask
      ,
  };
  return states;
}

// ----------------------------------------------------------------------------

bool DecompressDracoPrimitive(cgltf_primitive const& prim, std::vector<VertexInternal_t>& vertices, std::vector<uint32_t>& indices) {
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

// ----------------------------------------------------------------------------

void ExtractPrimitiveVertices(cgltf_primitive const& prim, std::vector<VertexInternal_t>& vertices) {
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
        // vec3 t3 = vec3(linalg::mul(world_matrix, vec4(lina::to_vec3(tangent), 0.0f)));
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

// ----------------------------------------------------------------------------

// std::string GetImageRefID(cgltf_image const* image, std::string_view alt) {
//   return std::string{
//     image->name ? image->name : (image->uri ? image->uri : std::string(alt))
//   };
// }

} // namespace ""

/* -------------------------------------------------------------------------- */

namespace internal::gltf_loader {

PointerToSamplerMap_t ExtractSamplers(
  cgltf_data const* data,
  std::vector<scene::Sampler>& samplers
) {
  PointerToSamplerMap_t samplers_lut{
    // The glTF spec allow for unspecified sampler on texture, so we define
    // one by default as fallback.
    {nullptr, {}}
  };

  for (cgltf_size sampler_id = 0; sampler_id < data->samplers_count; ++sampler_id) {
    cgltf_sampler const& gl_sampler = data->samplers[sampler_id];
    samplers.emplace_back(ConvertSamplerInfo(gl_sampler));
    samplers_lut.try_emplace( &gl_sampler, samplers.back() );
  }

  return samplers_lut;
}

// ----------------------------------------------------------------------------

PointerToIndexMap_t ExtractImages(
  cgltf_data const* data,
  std::vector<scene::ImageData>& images
) {
  PointerToIndexMap_t image_indices{};

  uint32_t const index_offset = static_cast<uint32_t>(images.size());
  // images.reserve(images.size() + data->images_count);

  stbi_set_flip_vertically_on_load(false); //
  for (cgltf_size image_id = 0; image_id < data->images_count; ++image_id) {
    cgltf_image const& gl_image = data->images[image_id];
    cgltf_buffer_view *buffer_view = gl_image.buffer_view;

    if (!buffer_view || !buffer_view->buffer) {
      continue;
    }

    stbi_uc const* buffer_data{
      reinterpret_cast<stbi_uc const*>(buffer_view->buffer->data) + buffer_view->offset
    };

    /* Image tasks should be retrieved outside this function via 'image->async_load_result()' */
    images.emplace_back();
    images.back().asyncLoad(buffer_data, buffer_view->size);

    uint32_t const image_index = index_offset + static_cast<uint32_t>(image_id);
    image_indices.try_emplace(&gl_image, image_index);
  }

  return image_indices;
}

// ----------------------------------------------------------------------------

PointerToIndexMap_t ExtractTextures(
  cgltf_data const* data,
  PointerToIndexMap_t const& image_indices,
  PointerToSamplerMap_t const& samplers_lut, //
  std::vector<scene::Texture>& textures
) {
  PointerToIndexMap_t textures_indices{};

  uint32_t const index_offset = static_cast<uint32_t>(textures.size());
  // textures.reserve(index_offset + data->textures_count);

  for (cgltf_size texture_id = 0; texture_id < data->textures_count; ++texture_id) {
    cgltf_texture const& gl_texture = data->textures[texture_id];

    if (gl_texture.sampler == nullptr) {
      LOGD("{} : empty sampler on glTF texture.", __FUNCTION__);
    }
    LOG_CHECK(image_indices.contains(gl_texture.image));
    LOG_CHECK(samplers_lut.contains(gl_texture.sampler));

    textures.emplace_back(
      image_indices.at(gl_texture.image),
      samplers_lut.at(gl_texture.sampler)
    );

    uint32_t const texture_index = index_offset + static_cast<uint32_t>(texture_id);
    textures_indices.try_emplace(&gl_texture, texture_index);
  }

  return textures_indices;
}

// ----------------------------------------------------------------------------

PointerToIndexMap_t ExtractMaterials(
  cgltf_data const* data,
  PointerToIndexMap_t const& textures_indices,
  std::vector<scene::MaterialProxy>& material_proxies,
  scene::ResourceBuffer<scene::MaterialRef>& material_refs,
  scene::MaterialProxy::TextureBinding const &bindings
) {
  PointerToIndexMap_t materials_indices{};

  auto get_texture = [&textures_indices]
    (cgltf_texture_view const& view, uint32_t _default_binding) {
      return view.texture ? textures_indices.at(view.texture) : _default_binding;
  };

  for (cgltf_size mat_id = 0; mat_id < data->materials_count; ++mat_id) {
    cgltf_material const& mat = data->materials[mat_id];
    auto const& pbr_mr = mat.pbr_metallic_roughness;

    auto const material_model{GetMaterialModel(mat)};

    if (scene::MaterialModel::Unknown == material_model) {
      LOGW("[GLTF] Material {} has unsupported material type.", uint32_t(mat_id));
      //continue;
    }

    scene::MaterialProxy proxy{
      .bindings = {
        .basecolor  = get_texture(pbr_mr.base_color_texture, bindings.basecolor),
        .normal     = get_texture(mat.normal_texture,     bindings.normal),
        .occlusion  = get_texture(mat.occlusion_texture,  bindings.occlusion),
        .emissive   = get_texture(mat.emissive_texture,   bindings.emissive),
        .roughness_metallic = get_texture(pbr_mr.metallic_roughness_texture, bindings.roughness_metallic),
      },
      .alpha_cutoff = mat.alpha_cutoff,
      .double_sided = static_cast<bool>(mat.double_sided),
      .pbr_mr = {
        .metallic_factor = pbr_mr.metallic_factor,
        .roughness_factor = pbr_mr.roughness_factor,
      },
    };
    std::copy(
      std::cbegin(mat.emissive_factor),
      std::cend(mat.emissive_factor),
      lina::ptr(proxy.emissive_factor)
    );
    std::copy(
      std::cbegin(pbr_mr.base_color_factor),
      std::cend(pbr_mr.base_color_factor),
      lina::ptr(proxy.pbr_mr.basecolor_factor)
    );

    material_proxies.push_back(proxy);
    uint32_t const material_index = static_cast<uint32_t>(material_proxies.size() - 1u);
    materials_indices.try_emplace(&mat, material_index); //

    material_refs.push_back( std::make_unique<scene::MaterialRef>(scene::MaterialRef{
      .model = material_model,
      .states = GetMaterialStates(mat),
      .proxy_index = material_index,
    }) );
  }

  return materials_indices;
}

// ----------------------------------------------------------------------------

PointerToIndexMap_t ExtractSkeletons(
  cgltf_data const* data,
  scene::ResourceBuffer<scene::Skeleton>& skeletons
) {
  PointerToIndexMap_t skeleton_indices{};

  return skeleton_indices;
}

// ----------------------------------------------------------------------------

void ExtractMeshes(
  cgltf_data const* data,
  PointerToIndexMap_t const& materials_indices,
  scene::ResourceBuffer<scene::MaterialRef> const& material_refs,
  PointerToIndexMap_t const& skeleton_indices,
  scene::ResourceBuffer<scene::Skeleton>const& skeletons,
  scene::ResourceBuffer<scene::Mesh>& meshes,
  std::vector<mat4f>& meshes_transforms,
  bool const bRestructureAttribs,
  bool const bForce32bitsIndex
) {
  /**
   * Each Mesh hold its geometry,
   * each primitive consist of a material and offset in the mesh geometry.
   * We assume every primitives of a Mesh have the same topology / attributes
   ***/

  /* Preprocess meshes nodes. */
  std::vector<uint32_t> meshNodeIndices{};
  for (cgltf_size i = 0; i < data->nodes_count; ++i) {
    cgltf_node const& node = data->nodes[i];
    if (node.mesh) {
      // mesh_count += node.mesh->primitives_count;
      meshNodeIndices.push_back(i);
      if (node.has_mesh_gpu_instancing) {
        LOGW("[GLTF] GPU instancing not supported.");
      }
    }
  }
  // meshes.reserve(meshNodeIndices.size());

  // Parse each mesh nodes (for primitives & skeleton).
  for (auto mesh_node_index : meshNodeIndices) {
    cgltf_node const& node = data->nodes[mesh_node_index];

    std::vector<uint32_t> valid_prim_indices{};
    // uint32_t total_vertex_count{0u};

    // -----------------
    // A. Preprocess primitives.
    for (cgltf_size prim_index = 0; prim_index < node.mesh->primitives_count; ++prim_index) {
      cgltf_primitive const& prim = node.mesh->primitives[prim_index];

      if (prim.attributes_count <= 0u) {
        LOGW("[GLTF] A primitive was missing attributes.");
        continue;
      }
      if (prim.has_draco_mesh_compression && (!kFrameworkHasDraco || !bRestructureAttribs)) {
        LOGW("[GLTF] Draco mesh compression is not supported.");
        continue;
      }
      if (prim.type != cgltf_primitive_type_triangles) {
        LOGW("[GLTF] Non TRIANGLES primitives are not supported.");
      }
      if (prim.targets_count > 0) {
        LOGW("[GLTF] Morph targets are not supported.");
      }
      bool is_sparse = false;
      for (cgltf_size k = 0; k < prim.attributes_count; ++k) {
        cgltf_attribute const& attribute = prim.attributes[k];
        cgltf_accessor const* accessor = attribute.data;
        if (accessor->is_sparse) {
          LOGW("[GLTF] Sparse attributes are not supported.");
          is_sparse = true;
          break;
        }
      }
      if (is_sparse) {
        continue;
      }

      // total_vertex_count += prim.attributes[0].data->count;
      valid_prim_indices.push_back(prim_index);
    }

    if (valid_prim_indices.empty()) {
      LOGW("[GLTF] A Mesh was bypassed due to unsupported features.");
      continue;
    }

    // -----------------
    // B. Create a new mesh.
    auto mesh = std::make_unique<scene::Mesh>();
    {
      meshes_transforms.emplace_back(linalg::identity);
      cgltf_node_transform_world(&node, lina::ptr(meshes_transforms.back()));
      mesh->submeshes.resize(valid_prim_indices.size(), {.parent = mesh.get()});
    }

    // -----------------
    // C. Retrieve its vertex attributes and indices.
    //
    //      The first branch force the attributes into a specific interleaved format
    //        allowing the engine to always assume the same layout.
    //
    //      The second take the attributes as is.
    //
    if (bRestructureAttribs) [[likely]] {
      mesh->set_attributes(VertexInternal_t::GetAttributeInfoMap());

      // XXX (Do not support different topology yet) XXX
      mesh->set_topology(Geometry::Topology::TriangleList); // xxx

      // Hold the interleaved attributes of the mesh in the same interleaved buffer.
      std::vector<VertexInternal_t> vertices{};

      // Offset to the primitive attributes inside the mesh buffer.
      uint64_t attribs_buffer_offset{0};

      /* Parse the primitives. */
      for (size_t prim_index = 0u; prim_index < valid_prim_indices.size(); ++prim_index) {
        uint32_t const valid_prim_index{ valid_prim_indices[prim_index] };
        cgltf_primitive const& prim{ node.mesh->primitives[valid_prim_index] };
        LOG_CHECK(prim.type == cgltf_primitive_type_triangles);

        Geometry::Primitive primitive{};
        primitive.topology = ConvertTopology(prim);

        if (prim.has_draco_mesh_compression) {
          std::vector<uint32_t> indices{};

          // Attributes & Indices.
          if (!DecompressDracoPrimitive(prim, vertices, indices)) {
            continue;
          }

          if (prim.indices) {
            mesh->set_index_format(Geometry::IndexFormat::U32);
            primitive.indexCount = static_cast<uint32_t>(indices.size());
            primitive.indexOffset = mesh->add_indices_data(std::as_bytes(std::span(indices)));
          }
        } else {
          // Attributes.
          ExtractPrimitiveVertices(prim, vertices);

          // Indices.
          if (prim.indices) {
            cgltf_accessor const* accessor = prim.indices;

            if (auto index_format = ConvertIndexFormat(accessor);
                index_format != Geometry::IndexFormat::kUnknown)
            {
              primitive.indexCount = accessor->count;

              cgltf_buffer_view const* buffer_view = accessor->buffer_view;
              cgltf_buffer const* buffer = buffer_view->buffer;

              size_t const index_size = cgltf_component_size(accessor->component_type);
              size_t const stride = accessor->stride ? accessor->stride : index_size;
              size_t const total_size = accessor->count * stride;

              std::byte const* src = reinterpret_cast<std::byte const*>(buffer->data) +
                                buffer_view->offset + accessor->offset;
              std::vector<uint32_t> indices_u32{};

              // [the same index format should be shared by the whole mesh.]
              if (bForce32bitsIndex && (index_format != Geometry::IndexFormat::U32)) [[likely]] {
                indices_u32.reserve(accessor->count);
                for (size_t i = 0; i < accessor->count; ++i) {
                  uint32_t val = 0;
                  switch (accessor->component_type)
                  {
                    case cgltf_component_type_r_16u:
                      val = *reinterpret_cast<uint16_t const*>(src + i * stride);
                    break;

                    case cgltf_component_type_r_8u:
                      val = *reinterpret_cast<uint8_t const*>(src + i * stride);
                    break;

                    default:
                      LOGE("Index 32bit convertion, unknown base format.");
                    break;
                  }
                  indices_u32.push_back(val);
                }
                mesh->set_index_format(Geometry::IndexFormat::U32);
                primitive.indexOffset = mesh->add_indices_data(
                  std::as_bytes(std::span(indices_u32))
                );
              } else {
                mesh->set_index_format(index_format);
                primitive.indexOffset = mesh->add_indices_data(std::span(src, total_size));
              }
            } else {
              LOGD("index format unsupported.");
            }
          }
        }

        /* Add the primitive interleaved attributes to the mesh, and retrieve its internal offset. */
        attribs_buffer_offset = mesh->add_vertices_data(std::as_bytes(std::span(vertices)));
        primitive.vertexCount = static_cast<uint32_t>(vertices.size());
        primitive.bufferOffsets = VertexInternal_t::GetAttributeOffsetMap(attribs_buffer_offset);

        // Material.
        if (prim.material) {
          uint32_t const material_index = materials_indices.at(prim.material);
          // mesh->submeshes[prim_index].material_proxy_index = material_index;
          mesh->submeshes[prim_index].material_ref = material_refs[ material_index ].get();
        }

        mesh->add_primitive(primitive);
      }
    } else {
      /* Utility function. */
      auto isAccessorOffsetFlat{[](cgltf_accessor const* acc) -> bool {
        size_t const kAccessorOffsetLimit = 2048;
        return (acc->offset == 0) || (acc->offset >= kAccessorOffsetLimit);
      }};

      /* Setup the shared attributes from the first primitive.
       * This assume all mesh primitives share the same layout.
       */
      {
        cgltf_primitive const& prim{ node.mesh->primitives[valid_prim_indices[0u]] };

        mesh->set_topology(ConvertTopology(prim));

        for (cgltf_size j = 0; j < prim.attributes_count; ++j) {
          cgltf_attribute const& attribute = prim.attributes[j];
          cgltf_accessor const* accessor = attribute.data;

          if (auto type = ConvertAttributeType(attribute); type != Geometry::AttributeType::kUnknown) {
            size_t const accessorOffset{ isAccessorOffsetFlat(accessor) ? 0u : accessor->offset };
            mesh->add_attribute(type, {
              .format = ConvertAttributeFormat(accessor),
              .offset = static_cast<uint32_t>(accessorOffset),
              .stride = static_cast<uint32_t>(accessor->stride),
            });
          }
        }
      }

      /* Parse the primitives. */
      for (uint32_t prim_index = 0u; prim_index < valid_prim_indices.size(); ++prim_index) {
        uint32_t const valid_prim_index = valid_prim_indices[prim_index];
        cgltf_primitive const& prim{ node.mesh->primitives[valid_prim_index] };
        LOG_CHECK(ConvertTopology(prim) == mesh->topology());

        Geometry::Primitive primitive{};

        /* Retrieve primitive attributes offsets, when relevant. */
        std::map<cgltf_accessor const*, uint64_t> accessor_buffer_offsets{};

        // Attributes.
        for (cgltf_size attrib_index = 0; attrib_index < prim.attributes_count; ++attrib_index) {
          cgltf_attribute const& attribute = prim.attributes[attrib_index];

          if (auto type = ConvertAttributeType(attribute); type != Geometry::AttributeType::kUnknown) {
            auto accessor = attribute.data;
            auto buffer_view = accessor->buffer_view;

            // (overwritten, but shared by all attributes as it's non-sparsed)
            primitive.vertexCount = accessor->count;

            uint64_t bufferOffset{};
            if (isAccessorOffsetFlat(accessor)) {
              bufferOffset = mesh->add_vertices_data(std::span<const std::byte>(
                  reinterpret_cast<const std::byte*>(buffer_view->buffer->data), buffer_view->size
                ).subspan(buffer_view->offset + accessor->offset)
              );
              accessor_buffer_offsets[accessor] = bufferOffset;
            } else {
              bufferOffset = accessor_buffer_offsets[accessor];
            }
            primitive.bufferOffsets[type] = bufferOffset;
          }
        }

        // Indices.
        if (prim.indices) {
          cgltf_accessor const* accessor = prim.indices;
          cgltf_buffer_view const* buffer_view = accessor->buffer_view;
          cgltf_buffer const* buffer = buffer_view->buffer;

          if (auto index_format = ConvertIndexFormat(accessor); index_format != Geometry::IndexFormat::kUnknown) {
            mesh->set_index_format(index_format);

            primitive.indexCount = accessor->count;
            primitive.indexOffset = mesh->add_indices_data(std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(buffer->data),
                buffer_view->size
              ).subspan(buffer_view->offset + accessor->offset)
            );
          }
        }

        // Material.
        if (prim.material) {
          uint32_t material_index = materials_indices.at(prim.material);
          // mesh->submeshes[prim_index].material_proxy_index = material_index;
          mesh->submeshes[prim_index].material_ref = material_refs[ material_index ].get();
        }

        mesh->add_primitive(primitive);
      }
    }

#if 0
    // -----------------
    // D. (optionnal) Retrieve the mesh skeleton.
    if (cgltf_skin const* skin = node.skin; skin) {

      cgltf_size const jointCount = skin->joints_count;

      auto skeleton = std::make_unique<scene::Skeleton>(jointCount);

      {


        // LUT Map to find parent nodes index.
        std::unordered_map<cgltf_node const*, int32_t> joint_indices(jointCount);
        for (cgltf_size jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
          cgltf_node const* joint = skin->joints[jointIndex];
          joint_indices[joint] = static_cast<int32_t>(jointIndex);
        }

        // Fill skeleton basic data (no matrices).
        for (cgltf_size jointIndex = 0; jointIndex < jointCount; ++jointIndex) {
          cgltf_node const* joint = skin->joints[jointIndex];

          std::string const jointName = joint->name ? joint->name
                                                    : std::string(basename + "::Joint_" + std::to_string(jointIndex));
          auto const& it = joint_indices.find(joint->parent);
          int32_t const parentIndex = (it != joint_indices.end()) ? it->second : -1;

          skeleton->names[jointIndex] = jointName;
          skeleton->parents[jointIndex] = parentIndex;
          skeleton->index_map[jointName] = static_cast<int32_t>(jointIndex);
        }

        // Calculates global inverse bind matrices.
        {
          // Retrieve local matrices.
          auto &matrices = skeleton->inverse_bind_matrices;
          auto const bufferSize = (sizeof(matrices[0]) / sizeof(*lina::ptr(matrices[0]))) * matrices.size();
          LOG_CHECK(bufferSize == (16 * jointCount));
          cgltf_accessor_unpack_floats(skin->inverse_bind_matrices, lina::ptr(matrices[0]), bufferSize);

          // Transform them to world space.
          auto const inverse_world_matrix{linalg::inverse(mesh->world_matrix)};
          skeleton->transformInverseBindMatrices(inverse_world_matrix);
        }

        skeletons_map[skinName] = skeleton;
      }

      mesh->skeleton = skeleton;
    }
#endif

    meshes.push_back( std::move(mesh) );
  }
}

// ----------------------------------------------------------------------------

void ExtractAnimations(
  cgltf_data const* data,
  std::string const& basename,
  scene::ResourceMap<scene::Skeleton>& skeletons_map,
  scene::ResourceMap<scene::AnimationClip>& animations_map
) {
  if (!data || (data->animations_count == 0u)) {
    return;
  }
  if (skeletons_map.empty()) {
    LOGE("[GLTF] animations without skeleton are not supported.");
    return;
  }

  auto resolve_name = [&basename](char const* cstr, std::string_view suffix, size_t index) -> std::string {
    return (cstr != nullptr) ? std::string(cstr)
                             : basename + std::string(suffix) + std::to_string(index);
  };

  // --------

  // LUT to find skeleton via animations name.
  std::unordered_map<std::string, std::string> animationName_to_SkeletonName;
  for (cgltf_size i = 0; i < data->skins_count; ++i) {
    cgltf_skin* skin = &data->skins[i];
    auto const skeletonName = resolve_name(skin->name, "::Skeleton_", i);
    for (size_t j = 0; j < skin->joints_count; ++j) {
      cgltf_node* joint_node = skin->joints[j];
      for (size_t k = 0; k < data->animations_count; ++k) {
        cgltf_animation const& animation = data->animations[k];
        auto const clipName = resolve_name(animation.name, "::Animation_", k);
        for (size_t l = 0; l < animation.channels_count; ++l) {
          cgltf_animation_channel* channel = &animation.channels[l];
          if (channel->target_node == joint_node) {
            animationName_to_SkeletonName[clipName] = skeletonName;
          }
        }
      }
    }
  }

  // --------

  std::vector<float> inputs, outputs;
  // animations_map.reserve(data->animations_count);

  // Retrieve Animations.
  for (cgltf_size i = 0; i < data->animations_count; ++i) {
    cgltf_animation const& animation = data->animations[i];
    LOG_CHECK(animation.samplers_count == animation.channels_count);

    std::string const clipName = resolve_name(animation.name, "::Animation_", i);

    // Find animation's skeleton.
    scene::Skeleton* skeleton{nullptr};
    if (auto skelname_it = animationName_to_SkeletonName.find(clipName); skelname_it != animationName_to_SkeletonName.end()) {
      auto skelname = skelname_it->second;
      if (auto skel_it = skeletons_map.find(skelname); skel_it != skeletons_map.end()) {
        skeleton = skel_it->second.get(); //
      }
    }
    if (nullptr == skeleton) {
      if (auto it = skeletons_map.begin(); it != skeletons_map.end()) {
        LOGW("[GLTF] used the first available skeleton found.");
        skeleton = it->second.get(); //
      } else {
        LOGE("[GLTF] cannot find animation's skeleton.");
        continue;
      }
    }

    // Preprocess channels to detect max sample count, in case the exporter compressed them.
    bool bResamplingNeededCheck = false;
    cgltf_size sampleCount{animation.channels[0].sampler->output->count};
    for (cgltf_size channel_id = 0; channel_id < animation.channels_count; ++channel_id) {
      auto const& channel = animation.channels[channel_id];
      auto const* sampler = channel.sampler;
      auto const* input = sampler->input;

      // We only support scalar sampling.
      LOG_CHECK(input->type == cgltf_type_scalar);

      // Check if the exporter have optimized animations by removing duplicate frames.
      bResamplingNeededCheck = (sampleCount > 1) && (sampleCount != sampler->output->count);

      inputs.resize(input->count);
      cgltf_accessor_unpack_floats(input, inputs.data(), inputs.size());
      sampleCount = std::max(sampleCount, sampler->output->count);
    }
    if (bResamplingNeededCheck) {
      LOGW("[GLTF] Some animations will be resampled.");
    }

    // Create the animation clip.
    auto clip = std::make_unique<scene::AnimationClip>();
    float const clipDuration = inputs.back();
    clip->setup(clipName, sampleCount, clipDuration, skeleton->jointCount());

    // Parse each channels (ie. transform per joint).
    cgltf_accessor const* last_input_accessor{nullptr};
    for (cgltf_size channel_id = 0; channel_id < animation.channels_count; ++channel_id) {
      auto const& channel{ animation.channels[channel_id] };
      auto const* sampler{ channel.sampler };
      auto const* input{ sampler->input };
      auto const* output{ sampler->output };

      // Find target joint's index.
      int32_t jointIndex = -1;
      if (auto jointName = channel.target_node->name; jointName != nullptr) {
        if (auto it = skeleton->index_map.find(jointName); it != skeleton->index_map.end()) {
          jointIndex = it->second;
        }
      }
      if (jointIndex < 0) {
        LOGE("[GLTF] cannot find joint index in LUT.");
        break;
      }

      // Inputs (eg. time).
      if (input != last_input_accessor) {
        inputs.resize(input->count);
        cgltf_accessor_unpack_floats(input, inputs.data(), inputs.size());
        last_input_accessor = input;
      }

      // Outputs (eg. rotation).
      cgltf_size const output_ncomp{cgltf_num_components(output->type)};
      outputs.resize(output_ncomp * output->count);
      cgltf_accessor_unpack_floats(output, outputs.data(), outputs.size());

      // Check if we need to resample outputs.
      bool const bNeedResampling{(sampleCount > 1) && (sampleCount != output->count)};
      cgltf_size frameStart{};
      cgltf_size frameEnd{};
      float lerpFactor{};

      for (cgltf_size sid = 0; sid < sampleCount; ++sid) {
        auto &pose = clip->poses[sid];
        auto &joint = pose.joints[jointIndex];

        // Calculate resampling parameters when needed.
        if (bNeedResampling) [[unlikely]] {
          // [optimization]
          // Most of the time there would be only two frames, the first and the last,
          // which would be identical and would not require any interpolation
          // (just copying them to the whole range).

          cgltf_size const src_sampleCount{ output->count };
          float const dt = sid / static_cast<float>(sampleCount - 1);

          frameStart = static_cast<int>(floor(dt * src_sampleCount));
          frameEnd = static_cast<int>(ceil(dt * src_sampleCount));
          frameStart = std::min(frameStart, src_sampleCount - 1);
          frameEnd = std::min(frameEnd, src_sampleCount - 1);

          float const start_time = inputs.at(frameStart);
          float const diff_time = inputs.at(frameEnd) - start_time;
          float const dst_time = dt * clipDuration;
          lerpFactor = (diff_time > 0.0f) ? (dst_time - start_time) / diff_time : 0.0f;
        }

        switch(channel.target_path) {
          case cgltf_animation_path_type_translation:
          {
            vec3f const* v = reinterpret_cast<vec3f const*>(outputs.data());
            joint.translation = bNeedResampling ? linalg::lerp(v[frameStart], v[frameEnd], lerpFactor)
                                                : v[sid]
                                                ;
          }
          break;

          case cgltf_animation_path_type_rotation:
          {
            vec4f const* q = reinterpret_cast<vec4f const*>(outputs.data());
            joint.rotation = bNeedResampling ? linalg::qnlerp(q[frameStart], q[frameEnd], lerpFactor)
                                             : q[sid]
                                             ;
          }
          break;

          case cgltf_animation_path_type_scale:
          {
            vec3f const* v = reinterpret_cast<vec3f const*>(outputs.data());
            vec3f s = bNeedResampling ? linalg::lerp(v[frameStart], v[frameEnd], lerpFactor)
                                      : v[sid]
                                      ;

            float const kTolerance = 1.0e-5f;
            if (lina::almost_equal(s[0], s[1], kTolerance)
             && lina::almost_equal(s[0], s[2], kTolerance)
             && lina::almost_equal(s[1], s[2], kTolerance))
            {
              joint.scale = s[0];
            }
            else
            {
              LOGW("[GLTF] Non-uniform scale not supported for skinning.");
            }
          }
          break;

          case cgltf_animation_path_type_weights:
          default:
            LOGW("[GLTF] Unsupported animation type.");
          break;
        }
      }
    }

    skeleton->clips.push_back(clip.get());

    animations_map.try_emplace(clipName, std::move(clip));
  }
}

} // namespace internal::gltf_loader

/* -------------------------------------------------------------------------- */