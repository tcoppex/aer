#ifndef AER_SCENE_ECS_HIERARCHY_H_
#define AER_SCENE_ECS_HIERARCHY_H_

#include <entt/entt.hpp>
#include "aer/core/common.h"

namespace scene {

/* -------------------------------------------------------------------------- */

namespace component {

/* Hierarchy relationships */
struct Node {
  entt::entity parent{entt::null};

  // siblings
  entt::entity next{entt::null};
  entt::entity prev{entt::null};

  // Children
  entt::entity firstChild{entt::null};
  size_t numChildren{0};
};

/* Local Transforms */
struct Transform {
  vec3 position{};
  quat rotation{linalg::identity};
  vec3 scale{1.0f, 1.0f, 1.0f};

  [[nodiscard]]
  mat4 matrix() const noexcept {
    return linalg::mul(
      linalg::mul(
        linalg::translation_matrix(position),
        linalg::rotation_matrix(rotation)
      ),
      linalg::scaling_matrix(scale)
    );
  }
};

/* World matrix for the entity */
struct GlobalTransform {
  mat4f worldMatrix{linalg::identity};
};

// -----------

struct Mesh {
  uint32_t meshIndex{kInvalidIndexU32};
};

} // namespace "component"

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

class Hierarchy {
 public:
  entt::registry registry{};
  entt::entity root{};

 public:
  using StagingGroup = decltype(registry.group<
    component::Node,
    component::Transform,
    component::GlobalTransform
  >());

  StagingGroup staging_group() {
   return registry.group<
      component::Node,
      component::Transform,
      component::GlobalTransform
    >();
  }

 public:
  Hierarchy() = default;
  ~Hierarchy() = default;

  void setup();

  void update();

  template<typename... Components>
  entt::entity createEntity(entt::entity parent) {
    auto entity = registry.create();

    // All entities requires a hierarchical component.
    registry.emplace<component::Node>(entity /*, parent*/);

    // Add additionnal optionnal components.
    (registry.emplace<Components>(entity), ...);

    // Move new entity to its parent.
    moveEntity(entity, parent);

    return entity;
  }

  template<typename... Components>
  entt::entity createStagingEntity(entt::entity parent) {
    return createEntity<
      component::Transform,
      component::GlobalTransform,
      Components...
    >(parent);
  }

  /* Move an entity to a new parent or the root when newParent is null. */
  void moveEntity(entt::entity e, entt::entity newParent);

 private:
  void updateGlobalTransform();

  void updateGlobalTransform(
    StagingGroup &group,
    entt::entity e,
    mat4 const& parent_matrix
  );
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_ECS_HIERARCHY_H_