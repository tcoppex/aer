#ifndef AER_SCENE_ECS_INSTANTIATE_H_
#define AER_SCENE_ECS_INSTANTIATE_H_

#include "aer/scene/asset_handle.h"
#include "aer/scene/ecs/hierarchy.h"

namespace scene {

struct Prefab;

/* Returns the instance root, or entt::null on failure.
 * scene_index < 0 selects prefab.default_scene. */
[[nodiscard]]
entt::entity Instantiate(
  Hierarchy&,
  Prefab const&,
  AssetHandle,
  entt::entity parent,
  mat4 const& local,
  int scene_index = -1
);

} // namespace "scene"

#endif // AER_SCENE_ECS_INSTANTIATE_H_