#ifndef AER_SCENE_ANIMATION_IK_H_
#define AER_SCENE_ANIMATION_IK_H_

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

namespace scene {

struct TwoBoneIK {
  uint32_t id{kInvalidIndexU32};              // IKId::index
  uint32_t root{kInvalidIndexU32};
  uint32_t mid{kInvalidIndexU32};
  uint32_t end{kInvalidIndexU32};
  vec3 target{};                              // in world space
  float weight{1.0f};
};

struct TwoBoneIKResult {
  quat root{lina::identity};
  quat mid{lina::identity};
  bool reached{false};
};

/* Solve a two-bone chain (root -> mid -> end) from its world space positions. */
[[nodiscard]]
TwoBoneIKResult SolveTwoBoneIK(
  vec3 const& root,
  vec3 const& mid,
  vec3 const& end,
  vec3 const& target
);

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_IK_H_