#ifndef AER_SCENE_ECS_ANIMATION_COMPONENTS_H_
#define AER_SCENE_ECS_ANIMATION_COMPONENTS_H_

#include <string>
#include <unordered_map>
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
  struct Fade {
    Pose from{};
    float time{};
    float duration{};
  };

  struct Blend {
    uint32_t tree_index{kInvalidIndexU32};
    float phase{};
    float duration{};
    Pose scratch{};
  };

  uint32_t clip{kInvalidIndexU32};
  float time{};
  float speed{1.0f};
  bool loop{true};
  bool finished{false};
  bool playing{false};
  bool resample{false};

  Pose pose{};
  Fade fade{};

  Blend blend{};
  std::vector<BlendTree1D> trees{};
  std::unordered_map<std::string, float> parameters{}; // (switch to vector?)

  AnimationStateMachineRuntime machine{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene::component"

#endif // AER_SCENE_ECS_ANIMATION_COMPONENTS_H_