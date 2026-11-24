#include "aer/scene/animation_ik.h"

#include <algorithm>
#include <cmath>

/* -------------------------------------------------------------------------- */

namespace scene {

TwoBoneIKResult SolveTwoBoneIK(
  vec3 const& root,
  vec3 const& mid,
  vec3 const& end,
  vec3 const& target,
  std::optional<vec3> const& pole
) {
  TwoBoneIKResult result{};

  vec3 const a = mid - root;
  vec3 const b = end - mid;
  vec3 const to_target = target - root;

  float const la = lina::length(a);
  float const lb = lina::length(b);
  float const lt = lina::length(to_target);
  if ((la < lina::kEpsilon) || (lb < lina::kEpsilon) || (lt < lina::kEpsilon)) {
    return result;
  }

  // Keep the target away from the singular poses (fully folded / fully straight).
  float const slack = 1.0e-4f * (la + lb);
  float const min_reach = std::abs(la - lb) + slack;
  float const max_reach = (la + lb) - slack;
  if (min_reach >= max_reach) {
    return result;
  }

  float const reach = std::clamp(lt, min_reach, max_reach);
  result.reached = (lt >= min_reach) && (lt <= max_reach);

  // Angle at the mid joint between the two bones, for a root -> end distance `d`.
  auto interior_angle = [&](float d) {
    float const c = (la * la + lb * lb - d * d) / (2.0f * la * lb);
    return std::acos(std::clamp(c, -1.0f, 1.0f));
  };
  float const bend = interior_angle(lina::length(end - root))
                   - interior_angle(reach);

  // Bend about the normal of the chain plane (arbitrary when the chain is straight).
  vec3 axis = lina::cross(a, b);
  float const axis_length = lina::length(axis);
  axis = (axis_length > lina::kEpsilon * la * lb)
       ? (axis / axis_length)
       : lina::any_perpendicular(a);
  result.mid = lina::rotation_quat(axis, bend);

  // Then swing the bent chain so its end points at the target.
  vec3 const axis_to_target = to_target / lt;
  vec3 const bent_end = a + lina::qrot(result.mid, b);
  result.root = lina::shortest_arc(lina::normalize(bent_end), axis_to_target);

  // Finally twist the chain about that axis so the mid joint faces the pole.
  if (pole) {
    vec3 const u = axis_to_target;
    vec3 const mid_dir = lina::qrot(result.root, a);
    vec3 const pole_dir = *pole - root;

    vec3 const from = mid_dir - u * lina::dot(mid_dir, u);
    vec3 const to = pole_dir - u * lina::dot(pole_dir, u);

    if ((lina::length(from) > lina::kEpsilon * la)
     && (lina::length(to) > lina::kEpsilon * (la + lb))) {
      float const angle = std::atan2(
        lina::dot(lina::cross(from, to), u),
        lina::dot(from, to)
      );
      result.root = lina::qmul(lina::rotation_quat(u, angle), result.root);
    }
  }

  return result;
}

}  // namespace "scene"

/* -------------------------------------------------------------------------- */