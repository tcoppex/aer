#ifndef AER_SCENE_PATH_2D_H_
#define AER_SCENE_PATH_2D_H_

#include "aer/core/common.h"
#include "aer/scene/polyline.h"

namespace scene {

class Mesh;

/* -------------------------------------------------------------------------- */

/* Represent a set of 2d closed shapes on the XY plane (Simple Polygon). */
class Path2D {
 public:
  using IndexBuffer = std::vector<uint32_t>;

  static constexpr uint32_t kDefaultLineToSubdivCount{ 1u };

  static constexpr float kDefaultExtrusionDepth{ 120.0f };
  static constexpr uint32_t kDefaultExtrusionSampleCount{ 4u };

 public:
  static
  bool BuildContourMesh(Path2D path, scene::Mesh &mesh);

  static
  bool BuildShapeMesh(
    Path2D path,
    scene::Mesh &mesh,
    float extrusionDepth = kDefaultExtrusionDepth,
    uint32_t extrusionSampleCount = kDefaultExtrusionSampleCount
  );

 public:
  Path2D() = default;

  Path2D(std::initializer_list<Polyline> polylines) {
    polylines_ = polylines;
  }

  void clear() noexcept {
    polylines_.clear();
  }

  void addContour(Polyline const& poly) {
    polylines_.push_back(poly);
  }

  void moveTo(vec2 const& p);

  void lineTo(
    vec2 const& p,
    uint32_t nsteps = kDefaultLineToSubdivCount
  );

  void quadBezierTo(
    vec2 const& cp,
    vec2 const& p,
    uint32_t curve_resolution = Polyline::kDefaultCurveResolution
  );

  /* Fill "index_buffers_" for the filled shapes. */
  bool triangulate();

  [[nodiscard]]
  bool triangulated() const noexcept {
    return !index_buffers_.empty();
  }

  [[nodiscard]]
  auto const& polylines() const noexcept {
    return polylines_;
  }

  [[nodiscard]]
  auto const& index_buffers() const noexcept {
    return index_buffers_;
  }

  [[nodiscard]]
  auto contour_subspan(size_t index) const {
    return std::span(polylines_).subspan(index, range_sizes_[index]);
  }

 private:
  [[nodiscard]]
  Polyline& last_polyline() {
    LOG_CHECK(!polylines_.empty());
    return polylines_.back();
  }

 private:
  std::vector<Polyline> polylines_{};

  // [filled by tesselate()]
  std::vector<IndexBuffer> index_buffers_{};
  std::vector<size_t> range_sizes_{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_PATH_2D_H_
