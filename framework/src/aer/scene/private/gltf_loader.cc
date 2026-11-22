#define CGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION

#include <string>
#include <utility>

#include "aer/scene/private/gltf_loader.h"
#include "aer/scene/private/extract_mesh_helpers.h"
#include "aer/scene/private/extract_animation_helpers.h"

/* -------------------------------------------------------------------------- */

namespace {

scene::Sampler ExtractSampler(cgltf_sampler const* s) {
  return s ? scene::Sampler(ConvertSamplerInfo(*s)) : scene::Sampler{};
}

// ---------------------------------

scene::MaterialModel GetMaterialModel(cgltf_material const& mat) {
  if (mat.unlit) {
    return scene::MaterialModel::Unlit;
  } else if (mat.has_pbr_metallic_roughness) {
    return scene::MaterialModel::PBRMetallicRoughness;
  }
  return scene::MaterialModel::Unknown;
}

scene::MaterialStates GetMaterialStates(cgltf_material const& mat) {
  return scene::MaterialStates{
    .alpha_mode = (mat.alpha_mode == cgltf_alpha_mode_opaque) ?
        scene::MaterialStates::AlphaMode::Opaque
      : (mat.alpha_mode == cgltf_alpha_mode_blend) ?
        scene::MaterialStates::AlphaMode::Blend
      : scene::MaterialStates::AlphaMode::Mask
      ,
  };
}

// ---------------------------------

void ReadNodeTransform(cgltf_node const& node, scene::component::Transform& t) {
  if (node.has_matrix) {
    auto const& m = reinterpret_cast<mat4 const&>(node.matrix);
    lina::decompose_transform_from_matrix(m, t.translation, t.rotation, t.scale);
  } else {
    if (node.has_translation) { t.translation = vec3(node.translation); }
    if (node.has_rotation)    { t.rotation    = quat(node.rotation); }
    if (node.has_scale)       { t.scale       = vec3(node.scale); }
  }
}

// Appends `node` and its subtree (parents first). Returns the node's prefab index.
uint32_t AppendPrefabNode(
  cgltf_data const* data,
  cgltf_node const* node,
  uint32_t parent_index,
  std::vector<uint32_t> const& mesh_remap,
  scene::Prefab& prefab
) {
  uint32_t const index = static_cast<uint32_t>(prefab.nodes.size());

  scene::PrefabNode pn{};
  pn.parent_index = parent_index;
  pn.source_node = static_cast<uint32_t>(cgltf_node_index(data, node));
  if (node->name) {
    pn.name = node->name;
  }
  ReadNodeTransform(*node, pn.transform);

  if (node->mesh) {
    pn.mesh_index = mesh_remap[cgltf_mesh_index(data, node->mesh)];  // invalid if skipped
    if (node->skin) {
      // (skeletons follow the glTF skins order)
      pn.skin_index = static_cast<uint32_t>(cgltf_skin_index(data, node->skin));
    }
  }
  if (node->has_mesh_gpu_instancing) {
    LOGW("[GLTF] GPU instancing not supported.");
  }

  // Push BEFORE recursing, and keep no reference into `nodes` across the calls.
  prefab.nodes.push_back(std::move(pn));

  for (cgltf_size i = 0; i < node->children_count; ++i) {
    AppendPrefabNode(data, node->children[i], index, mesh_remap, prefab);
  }
  return index;
}

} // namespace ""

/* -------------------------------------------------------------------------- */

namespace internal::gltf_loader {

void ExtractImages(
  cgltf_data const* data,
  std::vector<scene::ImageData>& images
) {
  // One ImageData per glTF image, so image index == glTF index.
  // asyncLoad captures `this`: the vector must never reallocate afterwards.
  LOG_CHECK(images.capacity() >= data->images_count);
  LOG_CHECK(images.empty());

  stbi_set_flip_vertically_on_load(false);
  for (cgltf_size i = 0; i < data->images_count; ++i) {
    auto& image = images.emplace_back();

    cgltf_buffer_view const* view = data->images[i].buffer_view;
    if (!view || !view->buffer || !view->buffer->data) {
      LOGW("[GLTF] image {} has no embedded data (external URIs unsupported).", uint32_t(i));
      continue;
    }

    auto const* bytes = reinterpret_cast<stbi_uc const*>(view->buffer->data) + view->offset;
    image.asyncLoad(bytes, static_cast<uint32_t>(view->size));
  }
}

// ----------------------------------------------------------------------------

void ExtractTextures(
  cgltf_data const* data,
  std::vector<scene::Texture>& textures
) {
  for (cgltf_size i = 0; i < data->textures_count; ++i) {
    cgltf_texture const& tex = data->textures[i];

    uint32_t image_index = kInvalidIndexU32;
    if (tex.image) {
      image_index = static_cast<uint32_t>(cgltf_image_index(data, tex.image));
    } else {
      LOGW("[GLTF] texture {} has no image.", uint32_t(i));
    }

    textures.emplace_back(image_index, ExtractSampler(tex.sampler));
  }
}

// ----------------------------------------------------------------------------

void ExtractMaterials(
  cgltf_data const* data,
  std::vector<scene::ImageData> const& images,
  std::vector<scene::Texture> const& textures,
  std::vector<scene::MaterialProxy>& proxies,
  std::vector<scene::MaterialRef>& refs
) {
  // Sentinel when the slot is unset OR its image is unusable;
  auto get_texture = [&](cgltf_texture_view const& view) -> uint32_t {
    if (!view.texture) {
      return kInvalidIndexU32;
    }
    auto const index = static_cast<uint32_t>(cgltf_texture_index(data, view.texture));
    uint32_t const image_index = textures[index].host_image_index;
    return (image_index != kInvalidIndexU32 && image_index < images.size())
         ? index : kInvalidIndexU32;
  };

  auto push = [&](scene::MaterialProxy proxy,
                  scene::MaterialModel model,
                  scene::MaterialStates states) {
    refs.push_back({
      .model = model,
      .states = states,
      .proxy_index = static_cast<uint32_t>(proxies.size()),
    });
    proxies.push_back(std::move(proxy));
  };

  for (cgltf_size i = 0; i < data->materials_count; ++i) {
    cgltf_material const& mat = data->materials[i];
    auto const& pbr = mat.pbr_metallic_roughness;

    auto const model = GetMaterialModel(mat);
    if (model == scene::MaterialModel::Unknown) {
      LOGW("[GLTF] Material {} has unsupported material type.", uint32_t(i));
    }

    scene::MaterialProxy proxy{
      .bindings = {
        .basecolor          = get_texture(pbr.base_color_texture),
        .normal             = get_texture(mat.normal_texture),
        .occlusion          = get_texture(mat.occlusion_texture),
        .emissive           = get_texture(mat.emissive_texture),
        .roughness_metallic = get_texture(pbr.metallic_roughness_texture),
      },
      .alpha_cutoff = mat.alpha_cutoff,
      .double_sided = static_cast<bool>(mat.double_sided),
      .pbr_mr = {
        .metallic_factor  = pbr.metallic_factor,
        .roughness_factor = pbr.roughness_factor,
      },
    };
    std::copy(std::cbegin(mat.emissive_factor), std::cend(mat.emissive_factor),
              lina::ptr(proxy.emissive_factor));
    std::copy(std::cbegin(pbr.base_color_factor), std::cend(pbr.base_color_factor),
              lina::ptr(proxy.pbr_mr.basecolor_factor));

    push(std::move(proxy), model, GetMaterialStates(mat));
  }

  // Default material (glTF spec) for primitives without one: always last.
  {
    scene::MaterialProxy proxy{
      .pbr_mr = {
        .basecolor_factor = vec4(1.0f),
        .metallic_factor  = 1.0f,
        .roughness_factor = 1.0f,
      }
    };
    push(std::move(proxy), scene::MaterialModel::PBRMetallicRoughness, {});
  }
}

// ----------------------------------------------------------------------------

[[nodiscard]]
std::vector<uint32_t> ExtractMeshes(
  cgltf_data const* data,
  uint32_t default_material,
  scene::LoadOptions const& opts,
  std::vector<scene::Mesh>& meshes
) {
  bool const restructure = !opts.keep_raw_attributes;
  bool const force32     = !opts.keep_indexing_format;

  // glTF mesh index -> index in `meshes` (invalid when the mesh was skipped).
  std::vector<uint32_t> mesh_remap(data->meshes_count, kInvalidIndexU32);

  // reused scratch buffers
  std::vector<VertexInternal_t> vertices{};
  std::vector<scene::SkinVertex> skin_vertices{};

  for (cgltf_size mesh_id = 0; mesh_id < data->meshes_count; ++mesh_id) {
    cgltf_mesh const& gl_mesh = data->meshes[mesh_id];

    scene::Mesh mesh{};
    if (restructure) {
      mesh.set_attributes(VertexInternal_t::GetAttributeInfoMap()); //
      mesh.set_topology(Geometry::Topology::TriangleList);
    }

    for (cgltf_size prim_id = 0; prim_id < gl_mesh.primitives_count; ++prim_id) {
      cgltf_primitive const& prim = gl_mesh.primitives[prim_id];

      if (!IsPrimitiveSupported(prim, restructure)) {
        continue;
      }

      Geometry::Primitive primitive{};
      primitive.topology = ConvertTopology(prim);

      bool const ok = restructure
        ? AppendPrimitiveRestructured(prim, force32, vertices, skin_vertices, mesh, primitive)
        : AppendPrimitiveRaw(prim, mesh, primitive);
      if (!ok) {
        continue;
      }

      // Both vectors are pushed at the same place, so they stay in sync.
      mesh.addPrimitive(primitive);
      mesh.primitive_materials_index.push_back(
        prim.material ? static_cast<uint32_t>(cgltf_material_index(data, prim.material))
                      : default_material
      );
    }

    if (mesh.primitive_count() == 0u) {
      LOGW("[GLTF] Mesh {} was bypassed due to unsupported features.", uint32_t(mesh_id));
      continue;
    }

    mesh_remap[mesh_id] = static_cast<uint32_t>(meshes.size());
    meshes.push_back(std::move(mesh));
  }

  return mesh_remap;
}

// ----------------------------------------------------------------------------

void ExtractSkeletons(
  cgltf_data const* data,
  std::vector<scene::Skeleton>& skeletons
) {
  skeletons.reserve(data->skins_count);

  for (cgltf_size s = 0; s < data->skins_count; ++s) {
    cgltf_skin const& skin = data->skins[s];

    // (one skeleton per skin, even if empty, to keep the indices in sync)
    auto& skeleton = skeletons.emplace_back(skin.joints_count);
    if (skin.name) {
      skeleton.name = skin.name;
    }

    std::unordered_map<cgltf_node const*, uint32_t> joint_of{};
    joint_of.reserve(skin.joints_count);
    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
      joint_of.emplace(skin.joints[j], static_cast<uint32_t>(j));
    }

    // Inverse bind matrices, identity when the skin has none.
    cgltf_accessor const* ibm = skin.inverse_bind_matrices;
    bool const has_ibm = ibm
                      && (ibm->type == cgltf_type_mat4)
                      && (ibm->count >= skin.joints_count);
    if (ibm && !has_ibm) {
      LOGW("[GLTF] skin {} has invalid inverse bind matrices, using identity.", uint32_t(s));
    }

    for (cgltf_size j = 0; j < skin.joints_count; ++j) {
      cgltf_node const* node = skin.joints[j];

      skeleton.joint_nodes[j] = static_cast<uint32_t>(cgltf_node_index(data, node));
      if (node->name) {
        skeleton.names[j] = node->name;
      }

      // Nearest ancestor which is also a joint (non joint nodes may sit between them).
      uint32_t parent = kInvalidIndexU32;
      for (cgltf_node const* p = node->parent; p != nullptr; p = p->parent) {
        if (auto const it = joint_of.find(p); it != joint_of.end()) {
          parent = it->second;
          break;
        }
      }
      skeleton.parents[j] = parent;

      auto& inverse_bind = skeleton.inverse_bind_matrices[j];
      if (has_ibm && !cgltf_accessor_read_float(ibm, j, lina::ptr(inverse_bind), 16)) {
        inverse_bind = mat4{lina::identity};
      }
      skeleton.global_bind_matrices[j] = lina::inverse(inverse_bind);

      if (!skeleton.names[j].empty()) {
        skeleton.index_map.try_emplace(skeleton.names[j], static_cast<uint32_t>(j));
      }
    }
  }
}

// ----------------------------------------------------------------------------

void ExtractAnimations(
  cgltf_data const* data,
  scene::LoadOptions const& opts,
  std::vector<scene::Skeleton>& skeletons
) {
  if (data->animations_count == 0u || skeletons.empty()) {
    return;
  }

  // glTF node -> every (skeleton, joint) it is the node of.
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> node_joints(data->nodes_count);
  for (size_t s = 0; s < skeletons.size(); ++s) {
    auto const& joint_nodes = skeletons[s].joint_nodes;
    for (size_t j = 0; j < joint_nodes.size(); ++j) {
      if (joint_nodes[j] < data->nodes_count) {
        node_joints[joint_nodes[j]].emplace_back(
          static_cast<uint32_t>(s), static_cast<uint32_t>(j)
        );
      }
    }
  }

  // Joints hold their rest pose when no channel drives them.
  std::vector<scene::Pose> rest_poses(skeletons.size());
  for (size_t s = 0; s < skeletons.size(); ++s) {
    auto const& joint_nodes = skeletons[s].joint_nodes;
    rest_poses[s].joints.resize(joint_nodes.size());
    for (size_t j = 0; j < joint_nodes.size(); ++j) {
      if (joint_nodes[j] < data->nodes_count) {
        ReadNodeTransform(data->nodes[joint_nodes[j]], rest_poses[s].joints[j]);
      }
    }
    skeletons[s].rest_pose = rest_poses[s]; // kept to restore the bind pose on stop
  }

  float const framerate = std::max(opts.animation_framerate, 1.0f);

  for (cgltf_size a = 0; a < data->animations_count; ++a) {
    cgltf_animation const& animation = data->animations[a];

    std::string const clip_name = animation.name
                                ? std::string(animation.name)
                                : ("animation_" + std::to_string(a));

    // Gather the channels driving joints, per skeleton.
    std::vector<std::vector<JointChannel>> skeleton_channels(skeletons.size());
    size_t ignored_count = 0u;

    for (cgltf_size c = 0; c < animation.channels_count; ++c) {
      cgltf_animation_channel const& channel = animation.channels[c];

      if (!channel.target_node) {
        ++ignored_count;
        continue;
      }
      auto const& targets = node_joints[cgltf_node_index(data, channel.target_node)];
      if (targets.empty()) {
        ++ignored_count;
        continue;
      }

      ChannelData channel_data{};
      if (!LoadChannelData(channel, channel_data)) {
        ++ignored_count;
        continue;
      }
      for (auto const& [skeleton_index, joint_index] : targets) {
        skeleton_channels[skeleton_index].push_back({joint_index, channel_data});
      }
    }

    if (ignored_count > 0u) {
      LOGW("[GLTF] animation \"{}\": {} channel(s) ignored (not driving a skin joint, "
           "morph targets or unsupported).", clip_name, ignored_count);
    }

    // Bake one clip per skeleton driven by the animation.
    for (size_t s = 0; s < skeletons.size(); ++s) {
      auto const& channels = skeleton_channels[s];
      if (channels.empty()) {
        continue;
      }

      float t_begin = std::numeric_limits<float>::max();
      float t_end   = std::numeric_limits<float>::lowest();
      for (auto const& channel : channels) {
        t_begin = std::min(t_begin, channel.data.times.front());
        t_end   = std::max(t_end,   channel.data.times.back());
      }
      float const duration = std::max(t_end - t_begin, 0.0f);

      // (a null duration gives a single static pose)
      uint32_t const sample_count = (duration > lina::kTrueEpsilon)
        ? static_cast<uint32_t>(std::ceil(duration * framerate)) + 1u
        : 1u;

      auto& skeleton = skeletons[s];
      auto& clip = skeleton.clips.emplace_back();
      clip.setup(clip_name, sample_count, duration, static_cast<uint32_t>(skeleton.jointCount()));

      for (uint32_t i = 0; i < sample_count; ++i) {
        float time = t_begin;
        if (sample_count > 1u) {
          time = (i + 1u == sample_count)
               ? t_end
               : t_begin + duration * (static_cast<float>(i) / static_cast<float>(sample_count - 1u));
        }

        auto& pose = clip.poses[i];
        pose = rest_poses[s];

        for (auto const& channel : channels) {
          vec4 const value = EvaluateChannel(channel.data, time);
          auto& joint = pose.joints[channel.joint];
          switch (channel.data.path) {
            case cgltf_animation_path_type_translation:
              joint.translation = value.xyz();
            break;

            case cgltf_animation_path_type_rotation:
              joint.rotation = value;
            break;

            case cgltf_animation_path_type_scale:
              joint.scale = value.xyz();
            break;

            default:
            break;
          }
        }
      }
    }
  }
}

// ----------------------------------------------------------------------------

[[nodiscard]]
std::shared_ptr<scene::Prefab> ExtractPrefab(
  cgltf_data const* data,
  std::vector<uint32_t> const& mesh_remap,
  std::vector<scene::Skeleton>&& skeletons
) {
  auto prefab = std::make_shared<scene::Prefab>();
  prefab->nodes.reserve(data->nodes_count);
  prefab->skeletons = std::move(skeletons);

  if (data->scenes_count > 0) {
    prefab->scenes.resize(data->scenes_count);
    for (cgltf_size s = 0; s < data->scenes_count; ++s) {
      cgltf_scene const& gl_scene = data->scenes[s];
      for (cgltf_size i = 0; i < gl_scene.nodes_count; ++i) {
        prefab->scenes[s].push_back(AppendPrefabNode(
          data, gl_scene.nodes[i], kInvalidIndexU32, mesh_remap, *prefab
        ));
      }
    }
    prefab->default_scene = data->scene
      ? static_cast<uint32_t>(cgltf_scene_index(data, data->scene))
      : 0u;
  } else {
    // Legal glTF without scenes: every root node forms the only scene.
    LOGW("[GLTF] file contains no scene, using all root nodes.");
    auto& roots = prefab->scenes.emplace_back();
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
      if (data->nodes[i].parent == nullptr) {
        roots.push_back(AppendPrefabNode(
          data, &data->nodes[i], kInvalidIndexU32, mesh_remap, *prefab
        ));
      }
    }
    prefab->default_scene = 0u;
  }

  return prefab;
}

} // namespace internal::gltf_loader

/* -------------------------------------------------------------------------- */
