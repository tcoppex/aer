#include <algorithm>
#include "aer/scene/polyline.h"

namespace scene {

/* -------------------------------------------------------------------------- */

void Polyline::quadBezierTo(
  vec2 const& cp,
  vec2 const& p,
  uint32_t curve_resolution
) {
  auto const dResolution = 1.0 / static_cast<double>(curve_resolution);
  auto const v = lina::to_vec2(vertices_.back());

  for (uint32_t i = 1; i <= curve_resolution; ++i) {
    auto const t = static_cast<float>(i * dResolution);
    auto const sampled_pt = lina::quadratic_bezier(v, cp, p, t);
    addVertex(sampled_pt);
  }
}

// ----------------------------------------------------------------------------

void Polyline::cubicBezierTo(
  vec2 const& cp0,
  vec2 const& cp1,
  vec2 const& p,
  uint32_t curve_resolution
) {
  auto const dResolution = 1.0 / static_cast<double>(curve_resolution);
  auto const v = lina::to_vec2(vertices_.back());

  for (uint32_t i = 1; i <= curve_resolution; ++i) {
    auto const t = static_cast<float>(i * dResolution);
    auto const sampled_pt = lina::cubic_bezier(v, cp0, cp1, p, t);
    addVertex(sampled_pt);
  }
}

// ----------------------------------------------------------------------------

float Polyline::signedArea2D(vec3 const axis) const noexcept {
  float area = 0.0f;
  for (size_t i = 0; i < vertices_.size(); ++i) {
    auto const& p1 = vertices_[i];
    auto const& p2 = vertices_[(i+1) % vertices_.size()];
    area += lina::dot(lina::cross(p1, p2), axis);
  }
  return area * 0.5f;
}

// ----------------------------------------------------------------------------

Polyline::Orientation Polyline::calculateOrientation2D(
  vec3 const axis
) const noexcept {
  auto const area = signedArea2D(axis);
  return area > 0.0f ? Orientation::CounterClockWise
       : area < 0.0f ? Orientation::ClockWise
       : Orientation::Degenerate
       ;
}

// ----------------------------------------------------------------------------

void Polyline::reverseOrientation() noexcept {
  std::reverse(vertices_.begin(), vertices_.end());
}

/* -------------------------------------------------------------------------- */

} // namespace "aer"