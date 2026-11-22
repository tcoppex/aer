#ifndef AER_SCENE_ECS_ANIMATION_COMPONENTS_H_
#define AER_SCENE_ECS_ANIMATION_COMPONENTS_H_

#include <vector>

#include "entt/entt.hpp"

#include "aer/core/common.h"
#include "aer/scene/animation.h"

/* -------------------------------------------------------------------------- */

//
// Animation Components
// (separated from components.h due to Animator::Pose ..)
//

namespace scene::component {

struct Skin {
  uint32_t skeleton_index{kInvalidIndexU32};  // index Prefab::skeletons
  Skeleton const* skeleton{};                 // same skeleton (valid while the instance lives)
  std::vector<entt::entity> joints{};
  std::vector<mat4> matrices{};               // Skin palette (identity at the bind pose)
};

// ----------------------------------------------------------------------------

/* One per (instance, skeleton), on the first node using that skeleton. */
struct Animator {
  uint32_t clip{kInvalidIndexU32};            // index Skeleton::clips
  float time{};
  float speed{1.0f};                          // negative = reverse
  bool loop{true};
  bool playing{false};
  bool resample{false};                       // sample once even if not playing
  Pose pose{};                                // last applied pose

  struct Fade {
    Pose from{};
    float time{};
    float duration{};
  } fade;
};

/* -------------------------------------------------------------------------- */

} // namespace "scene::component"

#endif // AER_SCENE_ECS_ANIMATION_COMPONENTS_H_