#ifndef AER_SCENE_ECS_COMPONENTS_H_
#define AER_SCENE_ECS_COMPONENTS_H_

#include <string>

#include "entt/entt.hpp"

#include "aer/core/common.h"
#include "aer/scene/asset_handle.h"

/* -------------------------------------------------------------------------- */

namespace scene::component {

/* Hierarchy relationships */
struct Node {
  entt::entity parent{entt::null};

  // siblings
  entt::entity next{entt::null};
  entt::entity prev{entt::null};

  // Children
  entt::entity firstChild{entt::null};
  size_t numChildren{0};
};

// ----------------------------------------------------------------------------

/* Local Transforms */
struct Transform {
  vec3 translation{};
  quat rotation{lina::identity};
  vec3 scale{1.0f, 1.0f, 1.0f};

  [[nodiscard]]
  mat4 toLocalMatrix() const noexcept {
    return lina::transform_matrix(translation, rotation, scale);
  }
};

// ----------------------------------------------------------------------------

/* World matrix for the entity */
struct GlobalTransform {
  mat4 worldMatrix{lina::identity};
};

// ----------------------------------------------------------------------------

struct Name {
  std::string name{};
};

// ----------------------------------------------------------------------------

// (sits on each subtree root)
struct Instance {
  AssetHandle asset{};
};

// ----------------------------------------------------------------------------

// (replace Mesh)
struct Renderable {
  AssetHandle asset{};
  uint32_t mesh_index{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene::component"

#endif // AER_SCENE_ECS_COMPONENTS_H_