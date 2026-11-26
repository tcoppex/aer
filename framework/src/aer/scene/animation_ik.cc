#include "aer/scene/animation_ik.h"

#include <algorithm>
#include <cmath>

/* -------------------------------------------------------------------------- */

namespace {

vec3 SafeDirection(vec3 const& v, vec3 const& fallback) {
  float const l = lina::length(v);
  return (l > lina::kEpsilon) ? (v / l) : fallback;
}

}  // namespace

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

// ----------------------------------------------------------------------------

FABRIKResult SolveFABRIK(
  std::span<vec3> positions,
  vec3 const& target,
  FABRIKParams const& params,
  std::optional<vec3> const& pole
) {
  FABRIKResult result{};

  size_t const n = positions.size();
  if (n < 2u) {
    return result;
  }

  std::vector<float> lengths(n - 1u);
  float total = 0.0f;
  for (size_t i = 0u; i + 1u < n; ++i) {
    lengths[i] = lina::length(positions[i + 1u] - positions[i]);
    if (lengths[i] < lina::kEpsilon) {
      return result;
    }
    total += lengths[i];
  }

  vec3 const root = positions[0];
  vec3 const to_target = target - root;
  float const distance = lina::length(to_target);

  // Out of reach: stretch straight toward the target.
  if (distance >= total) {
    vec3 const dir = to_target / distance;
    for (size_t i = 0u; i + 1u < n; ++i) {
      positions[i + 1u] = positions[i] + dir * lengths[i];
    }
    return result;
  }

  float const tolerance = std::max(params.tolerance, 1.0e-6f);
  vec3 const fallback{0.0f, 1.0f, 0.0f};

  for (; result.iterations < params.max_iterations; ++result.iterations) {
    if (lina::length(positions[n - 1u] - target) <= tolerance) {
      break;
    }

    // Backward: pin the end on the target.
    positions[n - 1u] = target;
    for (size_t i = n - 1u; i-- > 0u;) {
      vec3 const dir = SafeDirection(positions[i] - positions[i + 1u], fallback);
      positions[i] = positions[i + 1u] + dir * lengths[i];
    }

    // Forward: pin the root back.
    positions[0] = root;
    for (size_t i = 0u; i + 1u < n; ++i) {
      vec3 const dir = SafeDirection(positions[i + 1u] - positions[i], fallback);
      positions[i + 1u] = positions[i] + dir * lengths[i];
    }
  }
  result.reached = (lina::length(positions[n - 1u] - target) <= tolerance);

  // Rigid twist of the interior joints about the root -> end axis.
  if (pole && (n >= 3u)) {
    vec3 const axis = positions[n - 1u] - root;
    float const axis_length = lina::length(axis);
    if (axis_length > lina::kEpsilon) {
      vec3 const u = axis / axis_length;
      auto perp = [&](vec3 const& p) {
        vec3 const v = p - root;
        return v - u * lina::dot(v, u);
      };

      size_t farthest = 1u;
      float farthest_length = 0.0f;
      for (size_t i = 1u; i + 1u < n; ++i) {
        float const l = lina::length(perp(positions[i]));
        if (l > farthest_length) {
          farthest_length = l;
          farthest = i;
        }
      }

      vec3 const from = perp(positions[farthest]);
      vec3 const to = perp(*pole);
      float const min_length = 1.0e-4f * total;
      if ((farthest_length > min_length) && (lina::length(to) > min_length)) {
        float const angle = std::atan2(
          lina::dot(lina::cross(from, to), u),
          lina::dot(from, to)
        );
        quat const twist = lina::rotation_quat(u, angle);
        for (size_t i = 1u; i + 1u < n; ++i) {
          positions[i] = root + lina::qrot(twist, positions[i] - root);
        }
      }
    }
  }

  return result;
}

void ComputeChainDeltas(
  std::span<vec3 const> from,
  std::span<vec3 const> to,
  std::span<quat> deltas
) {
  size_t const n = std::min({from.size(), to.size(), deltas.size()});

  for (size_t i = 0u; i < n; ++i) {
    deltas[i] = quat{lina::identity};
  }
  for (size_t i = 0u; i + 1u < n; ++i) {
    vec3 const a = from[i + 1u] - from[i];
    vec3 const b = to[i + 1u] - to[i];
    float const la = lina::length(a);
    float const lb = lina::length(b);
    if ((la > lina::kEpsilon) && (lb > lina::kEpsilon)) {
      deltas[i] = lina::shortest_arc(a / la, b / lb);
    }
  }
}

// ----------------------------------------------------------------------------

vec3 LimitLookDirection(
  vec3 const& current,
  vec3 const& target,
  float max_angle,
  float fade_angle
) {
  quat const swing = lina::shortest_arc(current, target);
  float const angle = lina::qangle(swing);   // [0, pi]
  if (angle < lina::kEpsilon) {
    return target;
  }

  float const fade = std::max(fade_angle, lina::kEpsilon);
  float const factor = 1.0f - std::clamp((angle - max_angle) / fade, 0.0f, 1.0f);
  float const limited = std::min(angle, max_angle) * factor;

  return lina::qrot(lina::rotation_quat(lina::qaxis(swing), limited), current);
}

}  // namespace "scene"

/* -------------------------------------------------------------------------- */