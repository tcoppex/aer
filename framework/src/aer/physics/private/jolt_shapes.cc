#include "aer/physics/private/jolt_shapes.h"

#include <algorithm>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

/* -------------------------------------------------------------------------- */

namespace physics {

namespace {

struct ShapeFactory {
  float density;

  JPH::ShapeRefC finish(JPH::ConvexShapeSettings& settings) const {
    settings.SetDensity(density);

    auto const result = settings.Create();
    if (result.HasError()) {
      HLOGE("[physics] shape creation failed: {}", result.GetError().c_str());
      return nullptr;
    }
    return result.Get();
  }

  JPH::ShapeRefC operator()(BoxShape const& box) const {
    auto const& h = box.half_extents;
    float const smallest = std::min({h.x, h.y, h.z});
    if (smallest <= 0.0f) {
      HLOGE("[physics] box half extents must be positive.");
      return nullptr;
    }
    // Jolt rejects a convex radius bigger than the smallest half extent.
    JPH::BoxShapeSettings settings(ToJolt(h), std::min(JPH::cDefaultConvexRadius, smallest));
    return finish(settings);
  }

  JPH::ShapeRefC operator()(SphereShape const& sphere) const {
    if (sphere.radius <= 0.0f) {
      HLOGE("[physics] sphere radius must be positive.");
      return nullptr;
    }
    JPH::SphereShapeSettings settings(sphere.radius);
    return finish(settings);
  }

  JPH::ShapeRefC operator()(CapsuleShape const& capsule) const {
    if (capsule.half_height <= 0.0f || capsule.radius <= 0.0f) {
      HLOGE("[physics] capsule half height and radius must be positive.");
      return nullptr;
    }
    JPH::CapsuleShapeSettings settings(capsule.half_height, capsule.radius);
    return finish(settings);
  }
};

} // namespace

// ----------------------------------------------------------------------------

JPH::ShapeRefC CreateShape(ShapeDesc const& desc, float density) {
  return std::visit(ShapeFactory{density}, desc);
}

} // namespace "physics"

/* -------------------------------------------------------------------------- */