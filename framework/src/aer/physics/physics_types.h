#ifndef AER_PHYSICS_PHYSICS_TYPES_H_
#define AER_PHYSICS_PHYSICS_TYPES_H_

#include <functional>
#include <variant>

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

namespace physics {

struct WorldSettings {
  static constexpr uint32_t kDefaultMaxBodies{10240u};

  uint32_t max_body_pairs{1u << 16u};
  uint32_t max_bodies{kDefaultMaxBodies};
  uint32_t max_contact_constraints{kDefaultMaxBodies};
  size_t temp_allocator_size{kDefaultMaxBodies * 1024u};
  int32_t worker_threads{-1};
  vec3 gravity{0.0f, -9.81f, 0.0f};

  /* Fixed simulation step (seconds) used by PhysicsWorld::update. */
  float fixed_timestep{1.0f / 60.0f};

  uint32_t max_substeps{4u};
};

// ----------------------------------------------------------------------------

struct DebugOptions {
  bool enabled{false};
  bool shapes{true};
  bool bounding_boxes{false};
  bool centers_of_mass{false};
  bool velocities{false};
  bool constraints{false};
  bool sleeping_colors{false};
  bool depth_test{false};
  float line_width{1.0f};
};

/* Receives the debug segments of a world, colors are linear RGB. */
using DebugLineFn = std::function<void(vec3 const& a, vec3 const& b, vec3 const& color)>;

// ----------------------------------------------------------------------------

/* Opaque body handle */
using BodyId = uint32_t;
inline constexpr BodyId kInvalidBodyId{kInvalidIndexU32};

enum class MotionType : uint8_t {
  Static,     // never moves
  Kinematic,  // moved by the user, pushes dynamic bodies
  Dynamic,    // fully simulated
};

// ----------------------------------------------------------------------------

struct BoxShape {
  vec3 half_extents{0.5f, 0.5f, 0.5f};
};

struct SphereShape {
  float radius{0.5f};
};

struct CapsuleShape {
  float half_height{0.5f};
  float radius{0.25f};
};

using ShapeDesc = std::variant<BoxShape, SphereShape, CapsuleShape>;

// ----------------------------------------------------------------------------

struct BodyDesc {
  ShapeDesc  shape{BoxShape{}};
  MotionType motion{MotionType::Dynamic};
  vec3       position{};
  quat       rotation{lina::identity};
  vec3       linear_velocity{};
  float      friction{0.5f};
  float      restitution{0.0f};
  float      density{1000.0f};  // kg/m^3, ignored for static
  uint64_t   user_data{0u};
};

struct BodyPose {
  vec3 position{};
  quat rotation{lina::identity};
};

} // namespace "physics"

/* -------------------------------------------------------------------------- */

#endif // AER_PHYSICS_PHYSICS_TYPES_H_
