#ifndef AER_SCENE_ANIMATION_IK_H_
#define AER_SCENE_ANIMATION_IK_H_

#include <optional>
#include <functional>
#include <span>
#include <vector>

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

struct FABRIKParams {
  uint32_t max_iterations{16u};
  float tolerance{1.0e-3f};                 // end to target distance considered solved (m)
};

struct FABRIKResult {
  bool reached{false};                      // the end is within tolerance of the target
  uint32_t iterations{0u};
};

struct FABRIKLimit {
  float max_angle{lina::kPi};               // radians, swing from the reference direction (pi = free)
  bool hinge{false};                        // keep the animated bend plane, never fold backward
};

struct ChainIK {
  uint32_t id{kInvalidIndexU32};            // ChainIKId::index
  std::vector<uint32_t> joints{};           // root-most first, the last one is the end
  vec3 target{};                            // world space
  float weight{1.0f};
  float applied_weight{1.0f};               // weight used by the solver, moves toward `weight`
  float fade_rate{0.0f};                    // weight units per second (0 = immediate)
  vec3 pole{};
  bool use_pole{false};
  FABRIKParams params{};
  std::vector<FABRIKLimit> limits{};        // per bone from the root, empty = free
  //FABRIKResult result{};
};

/* Solve an N-joint chain (root-most first) in place, from its world space positions.
 * Bone lengths are kept and the root does not move. A target out of reach straightens
 * the chain toward it. With a `pole` (3+ joints), the interior joints are twisted about the
 * root -> end axis so the joint farthest from it faces the pole.
 * `limits` are enforced on the final pose, so the end may stop short of the target. */
[[nodiscard]]
FABRIKResult SolveFABRIK(
  std::span<vec3> positions,
  vec3 const& target,
  FABRIKParams const& params = {},
  std::optional<vec3> const& pole = std::nullopt,
  std::span<FABRIKLimit const> limits = {}
);

void ComputeChainDeltas(
  std::span<vec3 const> from,
  std::span<vec3 const> to,
  std::span<quat> deltas
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

// ----------------------------------------------------------------------------

struct LookAt {
  struct Link {
    uint32_t joint{kInvalidIndexU32};
    float weight{0.0f};
  };

  struct Params {
    vec3 aim{0.0f, 0.0f, 1.0f};           // local axis of the last joint that looks at the target
    float weight{1.0f};                   // target weight [0, 1]
    float max_angle{1.22f};               // in radians
    float fade_angle{0.7f};               // in radians
    float smooth{10.0f};
  };

  uint32_t id{kInvalidIndexU32};          // LookId::index, unique per AnimationSystem
  std::vector<Link> links{};              // root-most first, the last one aims
  vec3 target{};                          // world space

  Params params;

  float fade_rate{0.0f};
  float applied_weight{1.0f};
  vec3 direction{};                       // smoothed world aim direction
  bool has_direction{false};
};

/* Direction a look-at ends up with, from the current aim toward the desired direction (both unit).
 * The swing is limited to `max_angle` then fades back to zero over `fade_angle`.
 * Angles in radians. */
[[nodiscard]]
vec3 LimitLookDirection(
  vec3 const& current,
  vec3 const& target,
  float max_angle,
  float fade_angle
);

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_IK_H_