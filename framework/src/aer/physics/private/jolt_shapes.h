#ifndef AER_PHYSICS_PRIVATE_JOLT_SHAPES_H_
#define AER_PHYSICS_PRIVATE_JOLT_SHAPES_H_

#include "aer/physics/physics_types.h"
#include "aer/physics/private/jolt.h"

/* -------------------------------------------------------------------------- */

namespace physics {

[[nodiscard]]
JPH::ShapeRefC CreateShape(ShapeDesc const& desc, float density);

} // namespace "physics"

/* -------------------------------------------------------------------------- */

#endif // AER_PHYSICS_PRIVATE_JOLT_SHAPES_H_
