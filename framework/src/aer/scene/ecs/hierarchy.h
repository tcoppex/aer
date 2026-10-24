#ifndef AER_SCENE_ECS_HIERARCHY_H_
#define AER_SCENE_ECS_HIERARCHY_H_

#include "entt/entt.hpp"
#include "aer/scene/ecs/components.h"

namespace scene {

/* -------------------------------------------------------------------------- */

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

  // [todo]
  // ----------------------
  // void destroySubtree(entt::entity root);
  // entt::entity find(std::string_view path, entt::entity root = {}) const;
  // ----------------------

  // find("A/B/C", root) walks children from root,
  // comparing each Name against one path segment.
  // It’s O(children) per level, which is fine for lookups that happen
  // occasionally. If you later need hot lookups, build a per-instance cache then.

 private:
  void updateGlobalTransform(StagingGroup &group, entt::entity e, mat4 const& parent_matrix);
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_ECS_HIERARCHY_H_