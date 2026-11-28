#include "aer/scene/ecs/physics_system.h"

#include <algorithm>

#include "aer/scene/ecs/hierarchy.h"
#include "aer/scene/ecs/physics_components.h"

/* -------------------------------------------------------------------------- */

namespace scene {

PhysicsSystem::~PhysicsSystem() {
  release();
}

// ----------------------------------------------------------------------------

bool PhysicsSystem::setup(Hierarchy& scene, physics::WorldSettings const& settings) {
  if (!world_.init(settings)) {
    return false;
  }

  scene_ = &scene;
  scene_->registry
    .on_destroy<component::PhysicsBody>()
    .connect<&PhysicsSystem::onBodyDestroyed>(this);

  return true;
}

void PhysicsSystem::release() {
  if (scene_) {
    scene_->registry
      .on_destroy<component::PhysicsBody>()
      .disconnect<&PhysicsSystem::onBodyDestroyed>(this);
    scene_ = nullptr;
  }
  world_.release();
  last_elapsed_time_.reset();
}

// ----------------------------------------------------------------------------

void PhysicsSystem::update(float elapsed_time) {
  if (!scene_) {
    return;
  }

  float const dt = last_elapsed_time_
                 ? std::clamp(elapsed_time - *last_elapsed_time_, 0.0f, kMaxDeltaTime)
                 : 0.0f;
  last_elapsed_time_ = elapsed_time;

  createPendingBodies();

  if (paused_) {
    return;
  }

  if (world_.update(dt) > 0u) {
    writeBackTransforms();
  }
}

// ----------------------------------------------------------------------------

void PhysicsSystem::createPendingBodies() {
  auto& reg = scene_->registry;

  // Collect first, adding PhysicsBody while iterating its exclusion view is unsafe.
  pending_.clear();
  for (auto const e : reg.view<component::Collider>(entt::exclude<component::PhysicsBody>)) {
    pending_.push_back(e);
  }

  for (auto const e : pending_) {
    auto const& collider = reg.get<component::Collider>(e);

    physics::BodyDesc desc{};
    desc.shape       = collider.shape;
    desc.friction    = collider.friction;
    desc.restitution = collider.restitution;
    desc.user_data   = static_cast<uint64_t>(entt::to_integral(e));

    if (auto const* rigid = reg.try_get<component::RigidBody>(e)) {
      desc.motion          = rigid->motion;
      desc.density         = rigid->density;
      desc.linear_velocity = rigid->linear_velocity;
    } else {
      desc.motion = physics::MotionType::Static;
    }

    vec3 scale{};
    lina::decompose_transform_from_matrix(
      computeWorldMatrix(e), desc.position, desc.rotation, scale
    );

    auto const id = world_.createBody(desc);
    if (id == physics::kInvalidBodyId) {
      HLOGW("[physics] body creation failed for entity {}.", entt::to_integral(e));
    }

    // Always tag the entity, a failed one would be retried every frame otherwise.
    reg.emplace<component::PhysicsBody>(e, id);
  }
}

// ----------------------------------------------------------------------------

void PhysicsSystem::writeBackTransforms() {
  auto& reg = scene_->registry;
  auto const root = scene_->root;

  reg.view<
    component::PhysicsBody,
    component::RigidBody,
    component::Transform,
    component::Node
  >().each(
    [&](
      component::PhysicsBody const& body,
      component::RigidBody const& rigid,
      component::Transform& transform,
      component::Node const& node
    ) {
      if (rigid.motion != physics::MotionType::Dynamic
       || body.id == physics::kInvalidBodyId
       || !world_.isActive(body.id)) {
        return;
      }

      auto const pose = world_.bodyPose(body.id);

      // (the hierarchy root is expected to stay at identity)
      if (node.parent == entt::null || node.parent == root) {
        transform.translation = pose.position;
        transform.rotation    = pose.rotation;
        return;
      }

      // world -> local, using the parent's last updated global transform.
      mat4 const parent_world = reg.get<component::GlobalTransform>(node.parent).worldMatrix;
      mat4 const world = lina::transform_matrix(
        pose.position, pose.rotation, vec3{1.0f, 1.0f, 1.0f}
      );
      mat4 const local = lina::mul(lina::inverse(parent_world), world);

      vec3 scale{};
      lina::decompose_transform_from_matrix(
        local, transform.translation, transform.rotation, scale
      );
    }
  );
}

// ----------------------------------------------------------------------------

mat4 PhysicsSystem::computeWorldMatrix(entt::entity e) const {
  auto const& reg = scene_->registry;

  mat4 world{lina::identity};
  for (auto current = e; current != entt::null; current = reg.get<component::Node>(current).parent) {
    if (auto const* transform = reg.try_get<component::Transform>(current)) {
      world = lina::mul(transform->toLocalMatrix(), world);
    }
  }
  return world;
}

// ----------------------------------------------------------------------------

void PhysicsSystem::onBodyDestroyed(entt::registry& registry, entt::entity e) {
  world_.destroyBody(registry.get<component::PhysicsBody>(e).id);
}

} // namespace "scene"

/* -------------------------------------------------------------------------- */