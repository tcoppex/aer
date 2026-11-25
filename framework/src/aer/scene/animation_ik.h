#ifndef AER_SCENE_ANIMATION_IK_H_
#define AER_SCENE_ANIMATION_IK_H_

#include <optional>
#include <functional>

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
  float applied_weight{1.0f};               // weight used by the solver, moves toward `weight`
  float fade_rate{0.0f};                    // weight units per second (0 = immediate)
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

// ----------------------------------------------------------------------------

struct FootRig {
  struct Foot {
    uint32_t chain{kInvalidIndexU32};       // TwoBoneIK::id
    float offset{0.0f};                     // smoothed height change of the ground (m)
    vec3 normal{0.0f, 1.0f, 0.0f};          // smoothed ground normal
    float planted{0.0f};
  };

  struct Params {
    float ankle_height{0.0f};
    float probe_height{0.5f};
    float probe_depth{0.5f};
    float blend_height{0.15f};
    float smooth{12.0f};
    bool align{true};
    float max_angle{lina::kPi / 5.0f};      // in radians
  };

  std::vector<Foot> feet{};
  uint32_t pelvis{kInvalidIndexU32};        // joint index, ancestor of the chains' roots
  float pelvis_offset{0.0f};                // smoothed vertical offset (<= 0)
  Params params{};
};

struct GroundHit {
  vec3 position{};
  vec3 normal{0.0f, 1.0f, 0.0f};
};

using GroundQuery = std::function<
  bool(vec3 const& origin, float max_distance, GroundHit& hit)
>;

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_IK_H_