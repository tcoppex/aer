#ifndef AER_SCENE_POLYLINE_H_
#define AER_SCENE_POLYLINE_H_

#include "aer/core/common.h"
#include "aer/scene/geometry.h" // (for Geometry::AttributeInfo)

namespace scene {

/* -------------------------------------------------------------------------- */

/* Represent a curvature in 3D space. */
class Polyline {
 public:
  static constexpr uint32_t kDefaultCurveResolution{ 1u };
  static constexpr vec3 kDefaultFrontAxis{ 0, 0, 1 };

  using value_type = vec3;
  using size_type = std::size_t;
  using pointer = value_type*;
  using reference = value_type&;
  using const_reference = const value_type&;

  enum class Orientation {
    CounterClockWise,
    ClockWise,
    Degenerate,
  };

  [[nodiscard]]
  static Geometry::AttributeInfo AttributeInfo() noexcept {
    return {
      .format = Geometry::AttributeFormat::RGB_F32,
      .stride = sizeof(value_type),
    };
  }

 public:
  Polyline() = default;

  Polyline(std::initializer_list<vec3f> vertices) {
    addVertices(vertices);
  }

  Polyline(std::initializer_list<vec2f> vertices) {
    addVertices(vertices);
  }

  void clear() noexcept {
    vertices_.clear();
  }

  void addVertex(vec3 const& p) {
    vertices_.push_back(p);
  }

  void addVertex(vec2 const& p) {
    addVertex(lina::to_vec3(p));
  }

  void addVertices(std::initializer_list<vec3f> vertices) {
    vertices_ = vertices;
  }

  void addVertices(std::initializer_list<vec2f> vertices) {
    for (auto const& v : vertices) {
      addVertex(v);
    }
  }

  void quadBezierTo(
    vec2 const& cp,
    vec2 const& p,
    uint32_t curve_resolution = kDefaultCurveResolution
  ) {
    auto const dResolution = 1.0 / static_cast<double>(curve_resolution);
    auto const v = lina::to_vec2(vertices_.back());

    for (uint32_t i = 1; i <= curve_resolution; ++i) {
      auto const t = static_cast<float>(i * dResolution);
      auto const sampled_pt = lina::quadratic_bezier(v, cp, p, t);
      addVertex(sampled_pt);
    }
  }

  [[nodiscard]]
  float signedArea2D(vec3 const axis) const noexcept {
    float area = 0.0f;
    for (size_t i = 0; i < vertices_.size(); ++i) {
      auto const& p1 = vertices_[i];
      auto const& p2 = vertices_[(i+1) % vertices_.size()];
      area += linalg::dot(linalg::cross(p1, p2), axis);
    }
    return area * 0.5f;
  }

  [[nodiscard]]
  Orientation calculateOrientation2D(
    vec3 const axis = kDefaultFrontAxis
  ) const noexcept {
    auto const area = signedArea2D(axis);
    return area > 0 ? Orientation::CounterClockWise
         : area < 0 ? Orientation::ClockWise
         : Orientation::Degenerate
         ;
  }

  void reverseOrientation() noexcept {
    std::reverse(vertices_.begin(), vertices_.end());
  }

  [[nodiscard]]
  bool empty() const noexcept {
    return vertices_.empty();
  }

  [[nodiscard]]
  auto const& last_vertex() const noexcept {
    return vertices_.back();
  }

  [[nodiscard]]
  auto& vertices() noexcept {
    return vertices_;
  }

  [[nodiscard]]
  auto const& vertices() const noexcept {
    return vertices_;
  }

  [[nodiscard]]
  uint32_t vertex_count() const noexcept {
    return static_cast<uint32_t>(size());
  }

  [[nodiscard]]
  size_type size() const noexcept {
    return vertices_.size();
  }

  [[nodiscard]]
  reference operator[](size_type i) {
    return vertices_[i];
  }

  [[nodiscard]]
  const_reference operator[](size_type i) const {
    return vertices_[i];
  }

 private:
  std::vector<value_type> vertices_{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_POLYLINE_H_
