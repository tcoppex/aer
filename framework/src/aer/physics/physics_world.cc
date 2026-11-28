#include "aer/physics/physics_world.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <thread>

#include "aer/physics/private/jolt.h"
#include "aer/physics/private/jolt_layers.h"
#include "aer/physics/private/jolt_shapes.h"
#include "aer/physics/private/jolt_debug_renderer.h"

/* -------------------------------------------------------------------------- */

namespace physics {

namespace {

int g_runtime_refs = 0;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void TraceImpl(char const* fmt, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  HLOGD("[jolt] {}", buffer);
}

#ifdef JPH_ENABLE_ASSERTS
bool AssertFailedImpl(char const* expr, char const* msg, char const* file, JPH::uint line) {
  HLOGE("[jolt] assert {}:{} ({}) {}", file, line, expr, msg ? msg : "");
  return true;
}
#endif

void AcquireRuntime() {
  if (g_runtime_refs++ > 0) {
    return;
  }
  JPH::RegisterDefaultAllocator();
  JPH::Trace = TraceImpl;
#ifdef JPH_ENABLE_ASSERTS
  JPH::AssertFailed = AssertFailedImpl;
#endif
  JPH::Factory::sInstance = new JPH::Factory();
  JPH::RegisterTypes();
}

void ReleaseRuntime() {
  if (--g_runtime_refs > 0) {
    return;
  }
  JPH::UnregisterTypes();
  delete JPH::Factory::sInstance;
  JPH::Factory::sInstance = nullptr;
}

int ResolveWorkerThreads(int requested) {
  if (requested >= 0) {
    return requested;
  }
  auto const hw = static_cast<int>(std::thread::hardware_concurrency());
  return std::max(hw - 1, 0);
}

JPH::EMotionType ToJolt(MotionType motion) {
  switch (motion) {
    case MotionType::Static:    return JPH::EMotionType::Static;
    case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
    case MotionType::Dynamic:   return JPH::EMotionType::Dynamic;
  }
  return JPH::EMotionType::Static;
}

} // namespace

/* -------------------------------------------------------------------------- */

struct PhysicsWorld::Impl {
  Impl(WorldSettings const& s, int worker_threads)
    : settings(s)
    , temp_allocator(s.temp_allocator_size)
    , job_system(
        static_cast<JPH::uint>(JPH::cMaxPhysicsJobs),
        static_cast<JPH::uint>(JPH::cMaxPhysicsBarriers),
        worker_threads
      )
  {}

  JPH::BodyInterface& bodies() {
    return system.GetBodyInterface();
  }

  JPH::BodyInterface const& bodies() const {
    return system.GetBodyInterface();
  }

  WorldSettings                     settings;
  float                             accumulator{0.0f};

  // The layer interfaces must outlive `system`.
  layers::BroadPhaseLayerMapping    bp_layers{};
  layers::ObjectVsBroadPhaseFilter  object_vs_bp{};
  layers::ObjectPairFilter          object_pair{};

  JPH::TempAllocatorImpl            temp_allocator;
  JPH::JobSystemThreadPool          job_system;
  JPH::PhysicsSystem               system{};

#ifdef JPH_DEBUG_RENDERER
  // Created on the first drawDebug call.
  std::unique_ptr<DebugLineRenderer> debug_renderer{};
#endif
};

// ----------------------------------------------------------------------------

PhysicsWorld::PhysicsWorld() = default;

PhysicsWorld::~PhysicsWorld() {
  release();
}

// ----------------------------------------------------------------------------

bool PhysicsWorld::init(WorldSettings const& settings) {
  if (impl_) {
    HLOGW("PhysicsWorld is already initialized.");
    return true;
  }

  if (settings.fixed_timestep <= 0.0f || settings.max_substeps == 0u) {
    HLOGE("[physics] invalid fixed timestep settings.");
    return false;
  }

  AcquireRuntime();

  auto const threads = ResolveWorkerThreads(settings.worker_threads);
  impl_ = std::make_unique<Impl>(settings, threads);

  impl_->system.Init(
    settings.max_bodies,
    0u,
    settings.max_body_pairs,
    settings.max_contact_constraints,
    impl_->bp_layers,
    impl_->object_vs_bp,
    impl_->object_pair
  );
  impl_->system.SetGravity(ToJolt(settings.gravity));

  HLOGI(
    "Jolt {}.{}.{} ready ({} worker threads, {} max bodies).",
    JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH,
    threads, settings.max_bodies
  );
  return true;
}

// ----------------------------------------------------------------------------

void PhysicsWorld::release() {
  if (!impl_) {
    return;
  }
  impl_.reset();
  ReleaseRuntime();
}

// ----------------------------------------------------------------------------

uint32_t PhysicsWorld::update(float frame_dt) {
  if (!impl_) {
    return 0u;
  }

  auto const& s = impl_->settings;
  auto& accumulator = impl_->accumulator;

  accumulator += std::max(frame_dt, 0.0f);

  uint32_t steps = 0u;
  while (accumulator >= s.fixed_timestep && steps < s.max_substeps) {
    step(s.fixed_timestep);
    accumulator -= s.fixed_timestep;
    ++steps;
  }

  // Too far behind: drop the backlog instead of chasing it.
  if (accumulator >= s.fixed_timestep) {
    accumulator = std::fmod(accumulator, s.fixed_timestep);
  }
  return steps;
}

// ----------------------------------------------------------------------------

void PhysicsWorld::step(float dt, int collision_steps) {
  if (!impl_ || dt <= 0.0f) {
    return;
  }

  auto const error = impl_->system.Update(
    dt, collision_steps, &impl_->temp_allocator, &impl_->job_system
  );
  if (error != JPH::EPhysicsUpdateError::None) {
    HLOGW("Jolt update error flags: {}", static_cast<uint32_t>(error));
  }
}

// ----------------------------------------------------------------------------

float PhysicsWorld::alpha() const {
  return impl_ ? impl_->accumulator / impl_->settings.fixed_timestep : 0.0f;
}

// ----------------------------------------------------------------------------

void PhysicsWorld::submitDebugDraw(
  vec3 const& camera_position,
  DebugOptions const& options,
  DebugLineFn const& sink
) {
#ifdef JPH_DEBUG_RENDERER
  if (!impl_ || !sink) {
    return;
  }

  if (!impl_->debug_renderer) {
    impl_->debug_renderer = std::make_unique<DebugLineRenderer>();
  }
  auto& renderer = *impl_->debug_renderer;
  renderer.SetCameraPos(ToJolt(camera_position));
  renderer.begin(&sink);

  JPH::BodyManager::DrawSettings settings;
  settings.mDrawShape                 = options.shapes;
  settings.mDrawShapeWireframe        = true;
  settings.mDrawShapeColor            = options.sleeping_colors
                                      ? JPH::BodyManager::EShapeColor::SleepColor
                                      : JPH::BodyManager::EShapeColor::MotionTypeColor;
  settings.mDrawBoundingBox           = options.bounding_boxes;
  settings.mDrawCenterOfMassTransform = options.centers_of_mass;
  settings.mDrawVelocity              = options.velocities;
  impl_->system.DrawBodies(settings, &renderer);

  if (options.constraints) {
    impl_->system.DrawConstraints(&renderer);
    impl_->system.DrawConstraintLimits(&renderer);
  }

  renderer.end();
#else
  static_cast<void>(camera_position);
  static_cast<void>(options);
  static_cast<void>(sink);
#endif
}

// ----------------------------------------------------------------------------

void PhysicsWorld::set_gravity(vec3 const& gravity) {
  if (impl_) {
    impl_->system.SetGravity(ToJolt(gravity));
  }
}

// ----------------------------------------------------------------------------

vec3 PhysicsWorld::gravity() const {
  return impl_ ? FromJolt(impl_->system.GetGravity()) : vec3{};
}

// ----------------------------------------------------------------------------

BodyId PhysicsWorld::createBody(BodyDesc const& desc) {
  if (!impl_) {
    return kInvalidBodyId;
  }

  auto const shape = CreateShape(desc.shape, desc.density);
  if (!shape) {
    return kInvalidBodyId;
  }

  bool const is_static = (desc.motion == MotionType::Static);

  JPH::BodyCreationSettings settings(
    shape.GetPtr(),
    ToJolt(desc.position),
    ToJolt(desc.rotation),
    ToJolt(desc.motion),
    is_static ? layers::kStatic : layers::kMoving
  );
  settings.mLinearVelocity = ToJolt(desc.linear_velocity);
  settings.mFriction       = desc.friction;
  settings.mRestitution    = desc.restitution;
  settings.mUserData       = desc.user_data;

  auto const activation = (desc.motion == MotionType::Dynamic)
                        ? JPH::EActivation::Activate
                        : JPH::EActivation::DontActivate;

  auto const id = impl_->bodies().CreateAndAddBody(settings, activation);
  if (id.IsInvalid()) {
    HLOGW("[physics] no free body slot ({} max).", impl_->settings.max_bodies);
    return kInvalidBodyId;
  }
  return id.GetIndexAndSequenceNumber();
}

void PhysicsWorld::destroyBody(BodyId id) {
  if (!impl_ || id == kInvalidBodyId) {
    return;
  }
  auto& bodies = impl_->bodies();
  auto const body_id = ToJolt(id);
  if (bodies.IsAdded(body_id)) {
    bodies.RemoveBody(body_id);
  }
  bodies.DestroyBody(body_id);
}

// ----------------------------------------------------------------------------

uint32_t PhysicsWorld::body_count() const {
  return impl_ ? static_cast<uint32_t>(impl_->system.GetNumBodies()) : 0u;
}

// ----------------------------------------------------------------------------

BodyPose PhysicsWorld::bodyPose(BodyId id) const {
  BodyPose pose{};
  if (impl_ && id != kInvalidBodyId) {
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl_->bodies().GetPositionAndRotation(ToJolt(id), position, rotation);
    pose.position = FromJolt(position);
    pose.rotation = FromJolt(rotation);
  }
  return pose;
}

// ----------------------------------------------------------------------------

void PhysicsWorld::setBodyPose(BodyId id, vec3 const& position, quat const& rotation) {
  if (impl_ && id != kInvalidBodyId) {
    impl_->bodies().SetPositionAndRotation(
      ToJolt(id), ToJolt(position), ToJolt(rotation), JPH::EActivation::Activate
    );
  }
}

// ----------------------------------------------------------------------------

vec3 PhysicsWorld::linearVelocity(BodyId id) const {
  return (impl_ && id != kInvalidBodyId)
       ? FromJolt(impl_->bodies().GetLinearVelocity(ToJolt(id)))
       : vec3{};
}

// ----------------------------------------------------------------------------

void PhysicsWorld::setLinearVelocity(BodyId id, vec3 const& velocity) {
  if (impl_ && id != kInvalidBodyId) {
    impl_->bodies().SetLinearVelocity(ToJolt(id), ToJolt(velocity));
  }
}

// ----------------------------------------------------------------------------

void PhysicsWorld::addImpulse(BodyId id, vec3 const& impulse) {
  if (impl_ && id != kInvalidBodyId) {
    impl_->bodies().AddImpulse(ToJolt(id), ToJolt(impulse));
  }
}

// ----------------------------------------------------------------------------

bool PhysicsWorld::isActive(BodyId id) const {
  return impl_ && id != kInvalidBodyId && impl_->bodies().IsActive(ToJolt(id));
}

// ----------------------------------------------------------------------------

uint64_t PhysicsWorld::userData(BodyId id) const {
  return (impl_ && id != kInvalidBodyId) ? impl_->bodies().GetUserData(ToJolt(id)) : 0u;
}

} // namespace "physics"

/* -------------------------------------------------------------------------- */