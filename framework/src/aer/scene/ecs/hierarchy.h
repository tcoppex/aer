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
  quat rotation{lina::identity};
  vec3 scale{1.0f, 1.0f, 1.0f};
};

/* World matrix for the entity */
struct GlobalTransform {
  mat4f worldMatrix{lina::identity};
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
  using EntityMap = std::unordered_map<std::string, entt::entity>;

 public:
  entt::registry registry{};
  entt::entity root{};
  EntityMap entity_map{};

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

  template<typename... Components> [[nodiscard]]
  entt::entity createEntity(entt::entity parent) {
    auto entity = registry.create();

    // All entities requires a hierarchical component.
    registry.emplace<component::Node>(entity);

    // Add additionnal optionnal components.
    (registry.emplace<Components>(entity), ...);

    // Move new entity to its parent.
    moveEntity(entity, parent);

    return entity;
  }

  template<typename... Components> [[nodiscard]]
  entt::entity createStagingEntity(entt::entity parent) {
    return createEntity<
      component::Transform,
      component::GlobalTransform,
      Components...
    >(parent);
  }

  /* Move an entity to a new parent or the root when newParent is null. */
  void moveEntity(entt::entity e, entt::entity newParent);

  [[nodiscard]]
  entt::entity findByName(std::string_view entity_name) const;

 private:
  void updateGlobalTransform(StagingGroup &group, entt::entity e, mat4 const& parent_matrix);
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_ECS_HIERARCHY_H_