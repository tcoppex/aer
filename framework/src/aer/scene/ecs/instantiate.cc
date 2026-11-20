#include <unordered_map>
#include <vector>

#include "aer/scene/ecs/instantiate.h"

#include "aer/core/common.h"
#include "aer/scene/ecs/animation_system.h"
#include "aer/scene/ecs/components.h"
#include "aer/scene/asset.h"

namespace scene {

/* -------------------------------------------------------------------------- */

//
// Creates an instance root entity then creates the prefab’s nodes under it with their own TRS.
// The root is used to move, reparent or destroy it.
// If a glTF scene has several root nodes, the instance root is also what groups them.
//

entt::entity Instantiate(
  Hierarchy& scene,
  Prefab const& prefab,
  AssetHandle asset_handle,
  entt::entity parent,
  mat4 const& local,
  int scene_index
) {
  if (prefab.scenes.empty()) {
    LOGE("Instantiate: prefab has no scene.");
    return entt::null;
  }
  if (parent != entt::null && !scene.registry.valid(parent)) {
    LOGE("Instantiate: invalid parent entity.");
    return entt::null;
  }

  uint32_t const s = (scene_index < 0) ? prefab.default_scene
                                       : static_cast<uint32_t>(scene_index);
  if (s >= prefab.scenes.size()) {
    LOGE("Instantiate: scene index {} out of range ({} scenes).", s, prefab.scenes.size());
    return entt::null;
  }

  auto& reg = scene.registry;

  /* Instance root: the handle you move, reparent or destroy. */
  auto const instance_root = scene.createStagingEntity<>(parent);
  {
    auto& t = reg.get<component::Transform>(instance_root);
    lina::decompose_transform_from_matrix(local, t.translation, t.rotation, t.scale);
    reg.emplace<component::Instance>(instance_root, component::Instance{asset_handle});
  }

  std::vector<char> is_root(prefab.nodes.size(), 0);
  for (uint32_t r : prefab.scenes[s]) {
    is_root[r] = 1;
  }

  std::vector<entt::entity> created(prefab.nodes.size(), entt::entity{entt::null});
  for (uint32_t i = 0; i < prefab.nodes.size(); ++i) {
    auto const& node = prefab.nodes[i];

    entt::entity parent_entity;
    if (node.parent_index == kInvalidIndexU32) {
      if (!is_root[i]) {
        continue;
      }
      parent_entity = instance_root;
    } else {
      parent_entity = created[node.parent_index];
      if (parent_entity == entt::null) {
        continue;
      }
    }

    auto const e = scene.createStagingEntity<>(parent_entity);
    created[i] = e;

    reg.get<component::Transform>(e) = node.transform;
    if (!node.name.empty()) {
      reg.emplace<component::Name>(e, component::Name{node.name});
    }
    if (node.mesh_index != kInvalidIndexU32) {
      reg.emplace<component::Renderable>(e, component::Renderable{asset_handle, node.mesh_index});
    }
  }

  /* Skins and animators, once every node entity exists so joints can be resolved. */
  {
    // glTF node index -> entity (local to this instance, so instances never share joints).
    std::unordered_map<uint32_t, entt::entity> node_entity{};
    node_entity.reserve(prefab.nodes.size());
    for (uint32_t i = 0; i < prefab.nodes.size(); ++i) {
      if (created[i] != entt::null && prefab.nodes[i].source_node != kInvalidIndexU32) {
        node_entity.try_emplace(prefab.nodes[i].source_node, created[i]);
      }
    }

    std::vector<char> has_animator(prefab.skeletons.size(), 0);

    for (uint32_t i = 0; i < prefab.nodes.size(); ++i) {
      auto const& node = prefab.nodes[i];
      auto const e = created[i];
      if (e == entt::null || node.skin_index == kInvalidIndexU32) {
        continue;
      }
      if (node.skin_index >= prefab.skeletons.size()) {
        LOGW("Instantiate: skin index {} out of range ({} skeletons).",
             node.skin_index, prefab.skeletons.size());
        continue;
      }

      auto const& skeleton = prefab.skeletons[node.skin_index];
      auto const joint_count = skeleton.jointCount();

      auto& skin = reg.emplace<component::Skin>(e);
      skin.skeleton_index = node.skin_index;
      skin.joints.assign(joint_count, entt::entity{entt::null});
      skin.matrices.assign(joint_count, mat4{lina::identity});

      for (size_t j = 0; j < joint_count; ++j) {
        if (auto const it = node_entity.find(skeleton.joint_nodes[j]); it != node_entity.end()) {
          skin.joints[j] = it->second;
        }
      }

      // One animator per skeleton, even when several mesh nodes share the skin.
      if (!has_animator[node.skin_index]) {
        has_animator[node.skin_index] = 1;
        auto& animator = reg.emplace<component::Animator>(e);
        animator.skeleton = &skeleton;
      }
    }
  }

  return instance_root;
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"