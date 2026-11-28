#ifndef AER_PHYSICS_PHYSICS_WORLD_H_
#define AER_PHYSICS_PHYSICS_WORLD_H_

#include <memory>

#include "aer/core/common.h"
#include "aer/physics/physics_types.h"

/* -------------------------------------------------------------------------- */

namespace physics {

class PhysicsWorld {
 public:
  PhysicsWorld();
  ~PhysicsWorld();

  PhysicsWorld(PhysicsWorld const&) = delete;
  PhysicsWorld& operator=(PhysicsWorld const&) = delete;

  bool init(WorldSettings const& settings = {});
  void release();

  [[nodiscard]]
  bool is_valid() const noexcept {
    return impl_ != nullptr;
  }

  // --- Simulation ---

  /* Advance by a frame time: runs 0..max_substeps fixed steps.
   * Returns the number of steps taken. */
  uint32_t update(float frame_dt);

  /* Advance by exactly `dt` seconds (no accumulator). */
  void step(float dt, int collision_steps = 1);

  /* Fraction [0, 1) of a fixed step left in the accumulator (for render interpolation). */
  [[nodiscard]]
  float alpha() const;

  void submitDebugDraw(
    vec3 const& camera_position,
    DebugOptions const& options,
    DebugLineFn const& sink
  );

  void set_gravity(vec3 const& gravity);

  [[nodiscard]]
  vec3 gravity() const;

  // --- Bodies ---

  [[nodiscard]]
  BodyId createBody(BodyDesc const& desc);

  void destroyBody(BodyId id);

  [[nodiscard]]
  uint32_t body_count() const;

  [[nodiscard]]
  BodyPose bodyPose(BodyId id) const;

  /* Teleport a body (wakes it up). */
  void setBodyPose(BodyId id, vec3 const& position, quat const& rotation);

  [[nodiscard]]
  vec3 linearVelocity(BodyId id) const;

  void setLinearVelocity(BodyId id, vec3 const& velocity);

  /* Instant velocity change applied at the center of mass (kg.m/s). */
  void addImpulse(BodyId id, vec3 const& impulse);

  [[nodiscard]]
  bool isActive(BodyId id) const;

  [[nodiscard]]
  uint64_t userData(BodyId id) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_{};
};

} // namespace "physics"

/* -------------------------------------------------------------------------- */

#endif // AER_PHYSICS_PHYSICS_WORLD_H_