#ifndef AER_SCENE_ECS_PHYSICS_COMPONENTS_H_
#define AER_SCENE_ECS_PHYSICS_COMPONENTS_H_

#include "aer/core/common.h"
#include "aer/physics/physics_types.h"

/* -------------------------------------------------------------------------- */

//
// Physics Components
//

namespace scene::component {

/* Collision shape and surface. A Collider alone is a static body. */
struct Collider {
  physics::ShapeDesc shape{physics::BoxShape{}};
  float friction{0.5f};
  float restitution{0.0f};
};

// ----------------------------------------------------------------------------

/* Makes the entity move (absent = static). */
struct RigidBody {
  physics::MotionType motion{physics::MotionType::Dynamic};
  float density{1000.0f};   // kg/m^3
  vec3 linear_velocity{};   // initial velocity
};

// ----------------------------------------------------------------------------

/* Runtime state, owned by the PhysicsSystem (do not add it by hand).
 * An invalid id means the body failed to be created. */
struct PhysicsBody {
  physics::BodyId id{physics::kInvalidBodyId};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene::component"

#endif // AER_SCENE_ECS_PHYSICS_COMPONENTS_H_