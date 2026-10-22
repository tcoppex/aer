#ifndef AER_SCENE_FRUSTUM_H_
#define AER_SCENE_FRUSTUM_H_

#include <array>

#include "aer/core/common.h"

// ----------------------------------------------------------------------------

//
// View frustum made of 6 normalized, inward-facing planes.
//
// Extracted from a column-major view-projection matrix
// (right-handed, -Z forward, [0, 1] depth range).
//
class Frustum {
 public:
  using PlanesArray = std::array<float4, 6>;

 public:
  [[nodiscard]]
  static Frustum FromViewProj(float4x4 const& vp) {
    auto const row0 = vp.row(0);
    auto const row1 = vp.row(1);
    auto const row2 = vp.row(2);
    auto const row3 = vp.row(3);

    Frustum f{};
    f.planes_ = {
      lina::normalize_plane(row3 + row0), // Left
      lina::normalize_plane(row3 - row0), // Right
      lina::normalize_plane(row3 + row1), // Bottom
      lina::normalize_plane(row3 - row1), // Top
      lina::normalize_plane(row2),        // Near (Vulkan [0, 1] depth range)
      lina::normalize_plane(row3 - row2)  // Far  (Vulkan [0, 1] depth range)
    };
    return f;
  }

 public:
  Frustum() = default;

  /* Conservative AABB test, `margin` (world units) pads the frustum. */
  [[nodiscard]]
  bool intersectsAABB(
    float3 const& bmin,
    float3 const& bmax,
    float margin = 0.0f
  ) const noexcept {
    for (auto const& p : planes_) {
      float const x = (p.x >= 0.0f) ? bmax.x : bmin.x;
      float const y = (p.y >= 0.0f) ? bmax.y : bmin.y;
      float const z = (p.z >= 0.0f) ? bmax.z : bmin.z;
      if ((p.x * x + p.y * y + p.z * z + p.w + margin) < 0.0f) {
        return false;
      }
    }
    return true;
  }

 private:
  PlanesArray planes_{};
};

// ----------------------------------------------------------------------------

#endif // AER_SCENE_FRUSTUM_H_
