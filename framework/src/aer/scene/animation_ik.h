#ifndef AER_SCENE_ANIMATION_IK_H_
#define AER_SCENE_ANIMATION_IK_H_

#include <optional>

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

namespace scene {

struct TwoBoneIK {
  uint32_t id{kInvalidIndexU32};              // IKId::index
  uint32_t root{kInvalidIndexU32};
  uint32_t mid{kInvalidIndexU32};
  uint32_t end{kInvalidIndexU32};
  vec3 target{};
  float weight{1.0f};
  vec3 pole{};
  bool use_pole{false};
};

struct TwoBoneIKResult {
  quat root{lina::identity};
  quat mid{lina::identity};
  bool reached{false};
};

/* Solve a two-bone chain (root -> mid -> end) from its world space positions.
 * With a `pole`, the mid joint is rotated about the root -> target axis so it points
 * toward it (the bend plane contains root, target and pole). */
[[nodiscard]]
TwoBoneIKResult SolveTwoBoneIK(
  vec3 const& root,
  vec3 const& mid,
  vec3 const& end,
  vec3 const& target,
  std::optional<vec3> const& pole = std::nullopt
);

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_IK_H_