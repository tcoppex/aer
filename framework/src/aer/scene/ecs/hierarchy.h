#ifndef AER_SCENE_ECS_HIERARCHY_H_
#define AER_SCENE_ECS_HIERARCHY_H_

#include <string_view>
#include <vector>

#include "entt/entt.hpp"
#include "aer/scene/ecs/components.h"

namespace scene {

/* -------------------------------------------------------------------------- */

class Hierarchy {
 public:
  entt::registry registry{};
  entt::entity root{entt::null};

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

  template<typename... Components>
  decltype(auto) get(entt::entity e) {
    return registry.get<Components...>(e);
  }

  template<typename... Components>
  decltype(auto) get(entt::entity e) const {
    return registry.get<Components...>(e);
  }

  /* Move an entity to a new parent or the root when newParent is null. */
  void moveEntity(entt::entity e, entt::entity newParent);

  /* Entities of the subtree, `root` first (parents before children). */
  [[nodiscard]]
  std::vector<entt::entity> collectSubtree(entt::entity subtree_root) const;

  /* Detach `subtree_root` from its parent and destroy it and all descendants.
   * The hierarchy root cannot be destroyed. */
  void destroySubtree(entt::entity subtree_root);

  /* Resolve "A/B/C" by walking children from `from` (default: hierarchy root),
   * comparing each Name to one path segment. Returns entt::null if not found. */
  [[nodiscard]]
  entt::entity find(std::string_view path, entt::entity from = entt::null) const;

 private:
  void updateGlobalTransform(StagingGroup &group, entt::entity e, mat4 const& parent_matrix);
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_ECS_HIERARCHY_H_