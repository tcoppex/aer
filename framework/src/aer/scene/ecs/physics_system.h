#ifndef AER_SCENE_ECS_PHYSICS_SYSTEM_H_
#define AER_SCENE_ECS_PHYSICS_SYSTEM_H_

#include <optional>
#include <vector>

#include "entt/entt.hpp"

#include "aer/core/common.h"
#include "aer/physics/physics_world.h"

/* -------------------------------------------------------------------------- */

namespace scene {

class Hierarchy;

//
//    PhysicsSystem
//
//  Simulates the entities having a Collider (and optionally a RigidBody).
//
//  A frame is:
//      update(elapsed_time)    create the missing bodies, step the world,
//                              write the dynamic bodies back into their Transform
//      Hierarchy::update()     compute global transforms
//
class PhysicsSystem {
 public:
  static constexpr float kMaxDeltaTime = 0.1f;

 public:
  PhysicsSystem() = default;
  ~PhysicsSystem();

  // (the registry keeps a pointer to this instance)
  PhysicsSystem(PhysicsSystem const&) = delete;
  PhysicsSystem& operator=(PhysicsSystem const&) = delete;

  bool setup(Hierarchy& scene, physics::WorldSettings const& settings = {});

  void release();

  void update(float elapsed_time);

  /* While paused bodies are still created, but nothing is simulated. */
  void set_paused(bool paused) noexcept {
    paused_ = paused;
  }

  [[nodiscard]]
  bool paused() const noexcept {
    return paused_;
  }

  [[nodiscard]]
  physics::PhysicsWorld& world() noexcept {
    return world_;
  }

  [[nodiscard]]
  physics::PhysicsWorld const& world() const noexcept {
    return world_;
  }

  [[nodiscard]]
  physics::DebugOptions& debug() noexcept {
    return debug_;
  }

 private:
  void createPendingBodies();

  void writeBackTransforms();

  /* World matrix composed from the local Transforms (the GlobalTransform
   * of a new entity is not up to date yet). */
  [[nodiscard]]
  mat4 computeWorldMatrix(entt::entity e) const;

  void onBodyDestroyed(entt::registry& registry, entt::entity e);

 private:
  Hierarchy* scene_{};
  physics::PhysicsWorld world_{};

  std::optional<float> last_elapsed_time_{};
  bool paused_{false};
  physics::DebugOptions debug_{};

  std::vector<entt::entity> pending_{};
};

} // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ECS_PHYSICS_SYSTEM_H_