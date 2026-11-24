#ifndef AER_SCENE_ECS_ANIMATION_COMPONENTS_H_
#define AER_SCENE_ECS_ANIMATION_COMPONENTS_H_

#include <memory>
#include <vector>

#include "entt/entt.hpp"

#include "aer/core/common.h"
#include "aer/scene/animation.h"
#include "aer/scene/animation_nodes.h"

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

struct Animator {
  struct Fade {
    Pose from{};
    float time{};
    float duration{};
  };

  float speed{1.0f};
  bool finished{false};
  bool playing{false};
  bool resample{false};

  std::unique_ptr<PoseNode> root{};               // current motion (null = none)

  Pose pose{};
  Fade fade{};

  std::shared_ptr<AnimationGraph const> graph{};  // shared by the instances using it
  std::vector<float> parameters{};                // values, indexed by ParamId
  StateId state{};                                // current graph state (invalid = manual control)
};

/* -------------------------------------------------------------------------- */

} // namespace "scene::component"

#endif // AER_SCENE_ECS_ANIMATION_COMPONENTS_H_