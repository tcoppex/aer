#ifndef AER_SCENE_ECS_ANIMATION_SYSTEM_H_
#define AER_SCENE_ECS_ANIMATION_SYSTEM_H_

#include <vector>

#include "entt/entt.hpp"

#include "aer/core/common.h"
#include "aer/scene/animation.h"
#include "aer/scene/ecs/components.h"

/* -------------------------------------------------------------------------- */

//
// Animation components.
//

namespace scene::component {

//
// Sits on every node which has both a mesh and a skin.
//
struct Skin {
  uint32_t skeleton_index{kInvalidIndexU32};  // index Prefab::skeletons
  std::vector<entt::entity> joints{};
  std::vector<mat4> matrices{};               // Skin palette (identity at the bind pose)
};

// ----------------------------------------------------------------------------

//
// One per (instance, skeleton), on the first node using that skeleton.
//
struct Animator {
  Skeleton const* skeleton{};
  uint32_t clip{kInvalidIndexU32};  // index Skeleton::clips
  float time{};
  float speed{1.0f};
  bool loop{true};
  bool playing{false};
  Pose pose{};                      // last sampled pose
};

} // namespace "scene::component"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ECS_ANIMATION_SYSTEM_H_
