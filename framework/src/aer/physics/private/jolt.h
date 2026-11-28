#ifndef AER_PHYSICS_PRIVATE_JOLT_H_
#define AER_PHYSICS_PRIVATE_JOLT_H_

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/IssueReporting.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/Memory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/MotionType.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "aer/core/common.h"
#include "aer/physics/physics_types.h"

/* -------------------------------------------------------------------------- */

namespace physics {

[[nodiscard]] inline
JPH::Vec3 ToJolt(vec3 const& v) noexcept {
  return JPH::Vec3(v.x, v.y, v.z);
}

[[nodiscard]] inline
vec3 FromJolt(JPH::Vec3Arg v) noexcept {
  return vec3(v.GetX(), v.GetY(), v.GetZ());
}

[[nodiscard]] inline
JPH::Quat ToJolt(quat const& q) noexcept {
  return JPH::Quat(q.x, q.y, q.z, q.w);
}

[[nodiscard]] inline
quat FromJolt(JPH::QuatArg q) noexcept {
  return quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW());
}

[[nodiscard]] inline
JPH::BodyID ToJolt(BodyId id) noexcept {
  return JPH::BodyID(id);
}

} // namespace "physics"

/* -------------------------------------------------------------------------- */

#endif // AER_PHYSICS_PRIVATE_JOLT_H_