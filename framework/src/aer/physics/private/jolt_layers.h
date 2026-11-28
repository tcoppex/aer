#ifndef AER_PHYSICS_PRIVATE_JOLT_LAYERS_H_
#define AER_PHYSICS_PRIVATE_JOLT_LAYERS_H_

#include "aer/physics/private/jolt.h"

/* -------------------------------------------------------------------------- */

namespace physics::layers {

/* Object layers */
inline constexpr JPH::ObjectLayer kStatic{0};
inline constexpr JPH::ObjectLayer kMoving{1};
inline constexpr JPH::uint kNumObjectLayers{2};

/* Broad phase layers */
namespace bp {

inline constexpr JPH::BroadPhaseLayer kStatic{0};
inline constexpr JPH::BroadPhaseLayer kMoving{1};
inline constexpr JPH::uint kCount{2};

} // namespace "bp"

// ----------------------------------------------------------------------------

class BroadPhaseLayerMapping final : public JPH::BroadPhaseLayerInterface {
 public:
  BroadPhaseLayerMapping() {
    map_[kStatic] = bp::kStatic;
    map_[kMoving] = bp::kMoving;
  }

  JPH::uint GetNumBroadPhaseLayers() const override {
    return bp::kCount;
  }

  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
    JPH_ASSERT(layer < kNumObjectLayers);
    return map_[layer];
  }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  char const* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
    if (layer == bp::kStatic) return "STATIC";
    if (layer == bp::kMoving) return "MOVING";
    return "INVALID";
  }
#endif

 private:
  JPH::BroadPhaseLayer map_[kNumObjectLayers];
};

// ----------------------------------------------------------------------------

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad_phase) const override {
    if (object == kStatic) return broad_phase == bp::kMoving;   // static never hits static
    if (object == kMoving) return true;
    return false;
  }
};

// ----------------------------------------------------------------------------

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
    if (a == kStatic) return b == kMoving;
    if (a == kMoving) return true;
    return false;
  }
};

} // namespace "physics::layers"

/* -------------------------------------------------------------------------- */

#endif // AER_PHYSICS_PRIVATE_JOLT_LAYERS_H_
