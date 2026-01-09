#include "aer/scene/ecs/hierarchy.h"

namespace scene {

/* -------------------------------------------------------------------------- */

void Hierarchy::setup() {
  // Create a group of components often allocated together.
  staging_group();

  // Create the root entity.
  root = createStagingEntity(entt::null);
}

// ----------------------------------------------------------------------------

void Hierarchy::update() {
  // [todo]
  // Use a system to flag dirty matrices to only rebuild those who needs it.
  auto group = staging_group();
  updateGlobalTransform(group, root, linalg::identity);
}

// ----------------------------------------------------------------------------

void Hierarchy::moveEntity(entt::entity e, entt::entity newParent) {
  newParent = (newParent != entt::null) ? newParent : root;
  if (e == newParent || e == root || root == entt::null) {
    return;
  }

  auto& node = registry.get<component::Node>(e);
  if (node.parent == newParent) {
    return;
  }

  // DETACH from old parent
  if (node.parent != entt::null) {
    auto& oldParentNode = registry.get<component::Node>(node.parent);

    // If e was the first child, point parent to the next sibling
    if (oldParentNode.firstChild == e) {
      oldParentNode.firstChild = node.next;
    }

    // Update sibling links
    if (node.prev != entt::null) {
      registry.get<component::Node>(node.prev).next = node.next;
    }
    if (node.next != entt::null) {
      registry.get<component::Node>(node.next).prev = node.prev;
    }

    --oldParentNode.numChildren;
  }

  // ATTACH to new parent
  {
    auto& newParentNode = registry.get<component::Node>(newParent);

    node.parent = newParent;
    node.prev = entt::null;
    node.next = newParentNode.firstChild;

    // Update the old head of the list to point back to e
    if (newParentNode.firstChild != entt::null) {
      registry.get<component::Node>(newParentNode.firstChild).prev = e;
    }

    // Set parent's head to e
    newParentNode.firstChild = e;
    ++newParentNode.numChildren;
  }
}

// ----------------------------------------------------------------------------

entt::entity Hierarchy::findByName(std::string_view entity_name) const {
  if (auto it = entity_map.find(std::string(entity_name)); it != entity_map.end()) {
    return it->second;
  }
  return entt::null;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void Hierarchy::updateGlobalTransform(
  StagingGroup &group,
  entt::entity e,
  mat4 const& parent_matrix
) {
  auto [node, transform, global] = group.get(e);

  // [todo] cache local transform.
  auto const& localMatrix = lina::transform_matrix(
    transform.position,
    transform.rotation,
    transform.scale
  );

  global.worldMatrix = linalg::mul(parent_matrix, localMatrix);

  auto child = node.firstChild;
  while (child != entt::null) {
    updateGlobalTransform(group, child, global.worldMatrix);
    child = group.get<component::Node>(child).next; //
  }
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"
