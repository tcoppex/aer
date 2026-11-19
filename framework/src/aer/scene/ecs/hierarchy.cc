#include "aer/scene/ecs/hierarchy.h"

#define GET(compName, entity)   get<component::compName>(entity)
#define NODE(entity)            GET(Node, entity)

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
  updateGlobalTransform(group, root, lina::identity);
}

// ----------------------------------------------------------------------------

void Hierarchy::moveEntity(entt::entity e, entt::entity newParent) {
  newParent = (newParent != entt::null) ? newParent : root;
  if (e == newParent || e == root || root == entt::null) {
    return;
  }

  auto& node = NODE(e);
  if (node.parent == newParent) {
    return;
  }

  // DETACH from old parent
  if (node.parent != entt::null) {
    auto& oldParentNode = NODE(node.parent);

    // If e was the first child, point parent to the next sibling
    if (oldParentNode.firstChild == e) {
      oldParentNode.firstChild = node.next;
    }

    // Update sibling links
    if (node.prev != entt::null) {
      NODE(node.prev).next = node.next;
    }
    if (node.next != entt::null) {
      NODE(node.next).prev = node.prev;
    }

    --oldParentNode.numChildren;
  }

  // ATTACH to new parent
  {
    auto& newParentNode = NODE(newParent);

    node.parent = newParent;
    node.prev = entt::null;
    node.next = newParentNode.firstChild;

    // Update the old head of the list to point back to e
    if (newParentNode.firstChild != entt::null) {
      NODE(newParentNode.firstChild).prev = e;
    }

    // Set parent's head to e
    newParentNode.firstChild = e;
    ++newParentNode.numChildren;
  }
}

// ----------------------------------------------------------------------------

std::vector<entt::entity> Hierarchy::collectSubtree(entt::entity subtree_root) const {
  std::vector<entt::entity> out{};
  if (!registry.valid(subtree_root)) {
    return out;
  }
  std::vector<entt::entity> stack{subtree_root};
  while (!stack.empty()) {
    auto const e = stack.back();
    stack.pop_back();
    out.push_back(e);
    for (auto c = NODE(e).firstChild; c != entt::null; c = NODE(c).next) {
      stack.push_back(c);
    }
  }
  return out;
}

// ----------------------------------------------------------------------------

void Hierarchy::destroySubtree(entt::entity subtree_root) {
  if (subtree_root == root || !registry.valid(subtree_root)) {
    return;
  }

  /* Collect before touching links. */
  auto const entities = collectSubtree(subtree_root);

  /* Detach from the parent (same logic as moveEntity). */
  {
    auto& node = NODE(subtree_root);
    if (node.parent != entt::null) {
      auto& parent_node = NODE(node.parent);
      if (parent_node.firstChild == subtree_root) {
        parent_node.firstChild = node.next;
      }
      if (node.prev != entt::null) {
        NODE(node.prev).next = node.next;
      }
      if (node.next != entt::null) {
        NODE(node.next).prev = node.prev;
      }
      --parent_node.numChildren;
    }
  }

  registry.destroy(entities.begin(), entities.end());
}

// ----------------------------------------------------------------------------

entt::entity Hierarchy::find(std::string_view path, entt::entity from) const {
  auto current = (from != entt::null) ? from : root;
  if (!registry.valid(current)) {
    return entt::null;
  }

  while (!path.empty()) {
    auto const slash = path.find('/');
    auto const segment = path.substr(0, slash);
    path = (slash == std::string_view::npos) ? std::string_view{}
                                             : path.substr(slash + 1);
    if (segment.empty()) {
      continue;
    }

    auto next = entt::entity{entt::null};
    for (auto c = NODE(current).firstChild; c != entt::null; c = NODE(c).next) {
      auto const* name = registry.try_get<component::Name>(c);
      if (name && name->name == segment) {
        next = c;
        break;
      }
    }
    if (next == entt::null) {
      return entt::null;
    }
    current = next;
  }
  return current;
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
  auto const localMatrix{ transform.toLocalMatrix() };

  global.worldMatrix = lina::mul(parent_matrix, localMatrix);

  auto child = node.firstChild;
  while (child != entt::null) {
    updateGlobalTransform(group, child, global.worldMatrix);
    child = group.get<component::Node>(child).next; //
  }
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"
