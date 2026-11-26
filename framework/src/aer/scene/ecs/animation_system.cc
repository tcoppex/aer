#include "aer/scene/ecs/animation_system.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "aer/scene/animation_ik.h"
#include "aer/scene/ecs/hierarchy.h"
#include "aer/scene/ecs/animation_components.h"

/* -------------------------------------------------------------------------- */

namespace scene {

namespace {

using component::Animator;
using component::Skin;

[[nodiscard]]
mat4 PackDualQuat(lina::dualquat const& dq) {
  return mat4{dq.real, dq.dual, vec4{0.0f}, vec4{0.0f}};
}

[[nodiscard]]
bool IsInstanceRoot(entt::registry const& reg, entt::entity e) {
  return reg.valid(e) && reg.all_of<component::Instance>(e);
}

/* Calls fn(Animator&, Skin&) for the animators of an instance subtree. */
template<typename Fn>
void ForEachAnimator(entt::registry& reg, entt::entity instance_root, Fn&& fn) {
  std::vector<entt::entity> stack{instance_root};
  while (!stack.empty()) {
    auto const e = stack.back();
    stack.pop_back();

    auto* animator = reg.try_get<Animator>(e);
    auto* skin = reg.try_get<Skin>(e);
    if (animator && skin) {
      fn(*animator, *skin);
    }

    for (auto c = reg.get<component::Node>(e).firstChild; c != entt::null; c = reg.get<component::Node>(c).next) {
      if (!reg.all_of<component::Instance>(c)) {
        stack.push_back(c);
      }
    }
  }
}

/* Write a pose into the local Transform of the skin's joints. */
void ApplyPose(entt::registry& reg, Skin const& skin, Pose const& pose) {
  size_t const count = std::min(skin.joints.size(), pose.joints.size());
  for (size_t j = 0; j < count; ++j) {
    auto const joint = skin.joints[j];
    if (joint == entt::null || !reg.valid(joint)) {
      continue;
    }
    if (auto* transform = reg.try_get<component::Transform>(joint)) {
      *transform = pose.joints[j];
    }
  }
}

/* Make `motion` the root of the animator, cross-fading from the pose on screen. */
void StartMotion(
  Animator& animator,
  PoseCache& cache,
  std::unique_ptr<PoseNode> motion,
  float fade_duration
) {
  if ((fade_duration > 0.0f) && !animator.pose.joints.empty()) {
    auto snapshot = cache.acquire(animator.pose.joints.size());
    *snapshot = animator.pose;
    motion = std::make_unique<FadeNode>(
      std::move(motion), std::move(snapshot), fade_duration
    );
  }
  animator.root = std::move(motion);
  animator.playing = true;
  animator.finished = false;
  animator.resample = false;
}

void StartClip(
  Animator& animator,
  PoseCache& cache,
  Skeleton const& skeleton,
  uint32_t clip,
  bool loop,
  float fade_duration
) {
  LOG_CHECK(clip < skeleton.clips.size());
  StartMotion(
    animator,
    cache,
    std::make_unique<ClipNode>(skeleton.clips[clip], loop, animator.speed < 0.0f),
    fade_duration
  );
}

void StartBlendTree(
  Animator& animator,
  PoseCache& cache,
  uint32_t tree,
  float fade_duration
) {
  LOG_CHECK(animator.graph && tree < animator.graph->trees.size());
  StartMotion(
    animator,
    cache,
    std::make_unique<BlendNode1D>(animator.graph->trees[tree], animator.speed < 0.0f),
    fade_duration
  );
}

void EnterState(
  Animator& animator,
  PoseCache& cache,
  Skeleton const& skeleton,
  StateId state,
  float fade_duration
) {
  animator.state = state;

  auto const& resolved = animator.graph->states[state.index];
  if (resolved.tree != kInvalidIndexU32) {
    StartBlendTree(animator, cache, resolved.tree, fade_duration);
  } else {
    StartClip(animator, cache, skeleton, resolved.clip, resolved.loop, fade_duration);
  }
}

[[nodiscard]]
float ReadParameter(Animator const& animator, ParamId id) {
  return (id.valid() && id.index < animator.parameters.size())
       ? animator.parameters[id.index]
       : 0.0f;
}

// ----------------------------

void TickTriggers(Animator& animator, float dt) {
  if (!animator.graph) {
    return;
  }
  auto const& params = animator.graph->parameters;
  for (size_t i = 0; i < params.size() && i < animator.parameters.size(); ++i) {
    if (params[i].trigger
     && (params[i].expiry > 0.0f)
     && (animator.parameters[i] > 0.0f)) {
      animator.parameters[i] = std::max(animator.parameters[i] - dt, 0.0f);
    }
  }
}

// [TODO: make it non linear]
void TickIK(Animator& animator, float dt) {
  for (auto& chain : animator.ik) {
    if (chain.applied_weight == chain.weight) {
      continue;
    }
    if (chain.fade_rate <= 0.0f) {
      chain.applied_weight = chain.weight;
      continue;
    }
    float const delta = chain.weight - chain.applied_weight;
    float const step = chain.fade_rate * dt;
    chain.applied_weight = (std::abs(delta) <= step)
                         ? chain.weight
                         : chain.applied_weight + std::copysign(step, delta);
  }
}

/* Returns the number of animators of the instance having `id` as a trigger. */
uint32_t WriteTrigger(
  entt::registry& reg,
  entt::entity instance_root,
  ParamId id,
  bool pending
) {
  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] trigger: entity is not an instance root.");
    return 0u;
  }

  uint32_t count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!animator.graph
     || !id.valid()
     || (id.index >= animator.parameters.size())) {
      return;
    }
    auto const& param = animator.graph->parameters[id.index];
    if (!param.trigger) {
      return;
    }
    animator.parameters[id.index] = pending
      ? ((param.expiry > 0.0f) ? param.expiry : 1.0f)
      : 0.0f;
    ++count;
  });

  if (count == 0u) {
    LOGW("[Animation] trigger not found in this instance.");
  }
  return count;
}

// ----------------------------

template<typename Fn>
uint32_t ForEachIK(entt::registry& reg, entt::entity instance_root, IKId id, Fn&& fn) {
  uint32_t count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    for (auto& chain : animator.ik) {
      if (chain.id == id.index) {
        fn(chain);
        ++count;
      }
    }
  });
  return count;
}

[[nodiscard]]
entt::entity JointEntity(entt::registry const& reg, Skin const& skin, uint32_t joint) {
  if (joint >= skin.joints.size()) {
    return entt::entity{entt::null};
  }
  auto const e = skin.joints[joint];
  return reg.valid(e) ? e : entt::entity{entt::null};
}

struct WorldJoint {
  vec3 position{};
  quat rotation{lina::identity};
};

[[nodiscard]]
bool ReadWorld(entt::registry const& reg, entt::entity e, WorldJoint& out) {
  if (e == entt::null) {
    return false;
  }
  auto const* global = reg.try_get<component::GlobalTransform>(e);
  if (!global) {
    return false;
  }
  vec3 scale{};
  lina::decompose_transform_from_matrix(global->worldMatrix, out.position, out.rotation, scale);
  return true;
}

[[nodiscard]]
uint32_t ParentOf(Skeleton const& skeleton, uint32_t joint) {
  return (joint < skeleton.parents.size()) ? skeleton.parents[joint] : kInvalidIndexU32;
}

[[nodiscard]]
std::optional<TwoBoneIK> ResolveTwoBoneIK(
  Skeleton const& skeleton,
  AnimationSystem::TwoBoneIKDesc const& desc
) {
  auto parent_of = [&](uint32_t j) { return ParentOf(skeleton, j); };

  uint32_t const end = skeleton.findJoint(std::string{desc.end});
  if (end == kInvalidIndexU32) {
    return std::nullopt;
  }
  uint32_t const mid = desc.mid.empty() ? parent_of(end)
                                        : skeleton.findJoint(std::string{desc.mid});
  if (mid == kInvalidIndexU32) {
    return std::nullopt;
  }
  uint32_t const root = desc.root.empty() ? parent_of(mid)
                                          : skeleton.findJoint(std::string{desc.root});
  if (root == kInvalidIndexU32) {
    return std::nullopt;
  }

  if ((parent_of(end) != mid) || (parent_of(mid) != root)) {
    LOGW("[Animation] IK: \"{}\" is not a direct two-bone chain.", desc.end);
    return std::nullopt;
  }
  return TwoBoneIK{ .root = root, .mid = mid, .end = end };
}

void SolveIK(Hierarchy& scene, Animator const& animator, Skin const& skin) {
  auto& reg = scene.registry;

  for (auto const& chain : animator.ik) {
    if (chain.applied_weight <= 0.0f) {
      continue;
    }

    auto const e_root = JointEntity(reg, skin, chain.root);
    auto const e_mid  = JointEntity(reg, skin, chain.mid);
    auto const e_end  = JointEntity(reg, skin, chain.end);

    WorldJoint root{}, mid{}, end{};
    if (!ReadWorld(reg, e_root, root)
     || !ReadWorld(reg, e_mid, mid)
     || !ReadWorld(reg, e_end, end)) {
      continue;
    }

    auto* t_root = reg.try_get<component::Transform>(e_root);
    auto* t_mid  = reg.try_get<component::Transform>(e_mid);
    if (!t_root || !t_mid) {
      continue;
    }

    quat parent_rotation{lina::identity};
    if (auto const parent = reg.get<component::Node>(e_root).parent; parent != entt::null) {
      WorldJoint p{};
      if (ReadWorld(reg, parent, p)) {
        parent_rotation = p.rotation;
      }
    }

    std::optional<vec3> const pole = chain.use_pole ? std::optional<vec3>{chain.pole}
                                                    : std::nullopt;
    auto const ik = SolveTwoBoneIK(
      root.position, mid.position, end.position, chain.target, pole
    );

    // World deltas back to local rotations.
    quat const root_ik = lina::normalize(
      lina::qmul(lina::qinv(parent_rotation), ik.root, root.rotation)
    );
    quat const mid_ik = lina::normalize(
      lina::qmul(lina::qinv(root.rotation), ik.mid, mid.rotation)
    );

    float const w = std::min(chain.applied_weight, 1.0f);
    t_root->rotation = (w >= 1.0f) ? root_ik : lina::qlerp(t_root->rotation, root_ik, w);
    t_mid->rotation  = (w >= 1.0f) ? mid_ik  : lina::qlerp(t_mid->rotation,  mid_ik,  w);

    scene.updateSubtree(e_root);
  }
}

// ----------------------------

void StepWeight(float& applied, float target, float rate, float dt) {
  if (applied == target) {
    return;
  }
  if (rate <= 0.0f) {
    applied = target;
    return;
  }
  float const delta = target - applied;
  float const step = rate * dt;
  applied = (std::abs(delta) <= step) ? target : applied + std::copysign(step, delta);
}

void TickChain(Animator& animator, float dt) {
  for (auto& chain : animator.chain) {
    StepWeight(chain.applied_weight, chain.weight, chain.fade_rate, dt);
  }
}

/* Same behavior as setIKWeight: immediate, or a linear fade over `blend_seconds`. */
void SetFade(
  float& weight, float& applied, float& rate,
  float target, float blend_seconds
) {
  if (blend_seconds > 0.0f) {
    if (weight == target) {
      return;
    }
    weight = target;
    rate = std::abs(target - applied) / blend_seconds;
  } else {
    weight = target;
    applied = target;
    rate = 0.0f;
  }
}

template<typename Fn>
uint32_t ForEachChain(entt::registry& reg, entt::entity instance_root, ChainIKId id, Fn&& fn) {
  uint32_t count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    for (auto& chain : animator.chain) {
      if (chain.id == id.index) {
        fn(chain);
        ++count;
      }
    }
  });
  return count;
}

[[nodiscard]]
std::optional<ChainIK> ResolveChainIK(
  Skeleton const& skeleton,
  AnimationSystem::ChainIKDesc const& desc
) {
  uint32_t const end = skeleton.findJoint(std::string{desc.end});
  if (end == kInvalidIndexU32) {
    return std::nullopt;
  }

  uint32_t const bones = std::max(desc.length, 2u);
  std::vector<uint32_t> joints{end};
  for (uint32_t i = 0u; i < bones; ++i) {
    auto const parent = ParentOf(skeleton, joints.back());
    if (parent == kInvalidIndexU32) {
      LOGW("[Animation] chain IK: \"{}\" has fewer than {} bone(s) above it.", desc.end, bones);
      return std::nullopt;
    }
    joints.push_back(parent);
  }
  std::ranges::reverse(joints);   // root-most first

  ChainIK chain{};
  chain.joints = std::move(joints);
  chain.weight = std::clamp(desc.weight, 0.0f, 1.0f);
  chain.applied_weight = chain.weight;
  chain.params.max_iterations = std::clamp(desc.params.max_iterations, 1u, 64u);
  chain.params.tolerance = std::max(desc.params.tolerance, 1.0e-5f);
  return chain;
}

/* Solve the chains of an animator with FABRIK, runs after SolveIK.
 * Joints are rotated root to tip: each one gets its own solved world rotation
 * (delta * original world rotation) expressed in the space of its current parent. */
void SolveChainIK(Hierarchy& scene, Animator const& animator, Skin const& skin) {
  auto& reg = scene.registry;

  std::vector<entt::entity> entities{};
  std::vector<vec3> from{};
  std::vector<vec3> to{};
  std::vector<quat> rotations{};
  std::vector<quat> deltas{};

  for (auto const& chain : animator.chain) {
    size_t const n = chain.joints.size();
    if ((chain.applied_weight <= 0.0f) || (n < 2u)) {
      continue;
    }

    entities.clear();
    from.clear();
    rotations.clear();

    bool valid = true;
    for (auto const joint : chain.joints) {
      auto const e = JointEntity(reg, skin, joint);
      WorldJoint world{};
      if (!ReadWorld(reg, e, world) || !reg.all_of<component::Transform>(e)) {
        valid = false;
        break;
      }
      entities.push_back(e);
      from.push_back(world.position);
      rotations.push_back(world.rotation);
    }
    if (!valid) {
      continue;
    }

    to = from;
    std::optional<vec3> const pole = chain.use_pole ? std::optional<vec3>{chain.pole}
                                                    : std::nullopt;
    (void)SolveFABRIK(to, chain.target, chain.params, pole);

    deltas.assign(n, quat{lina::identity});
    ComputeChainDeltas(from, to, deltas);

    float const w = std::min(chain.applied_weight, 1.0f);

    // The end joint has no bone: it keeps its local rotation.
    for (size_t i = 0u; i + 1u < n; ++i) {
      auto& transform = reg.get<component::Transform>(entities[i]);

      quat parent_rotation{lina::identity};
      if (auto const parent = reg.get<component::Node>(entities[i]).parent; parent != entt::null) {
        WorldJoint p{};
        if (ReadWorld(reg, parent, p)) {
          parent_rotation = p.rotation;
        }
      }

      quat const solved = lina::normalize(
        lina::qmul(lina::qinv(parent_rotation), deltas[i], rotations[i])
      );
      transform.rotation = (w >= 1.0f) ? solved : lina::qlerp(transform.rotation, solved, w);

      scene.updateSubtree(entities[i]);
    }
  }
}

// ----------------------------

[[nodiscard]]
TwoBoneIK* FindChain(Animator& animator, uint32_t id) {
  auto const it = std::ranges::find(animator.ik, id, &TwoBoneIK::id);
  return (it != animator.ik.end()) ? &*it : nullptr;
}

[[nodiscard]]
bool IsAncestor(Skeleton const& skeleton, uint32_t ancestor, uint32_t joint) {
  for (size_t i = 0u; i < skeleton.parents.size(); ++i) {
    joint = ParentOf(skeleton, joint);
    if (joint == kInvalidIndexU32) {
      return false;
    }
    if (joint == ancestor) {
      return true;
    }
  }
  return false;
}

[[nodiscard]]
entt::entity FindInstanceRoot(entt::registry const& reg, entt::entity e) {
  while (e != entt::null) {
    if (reg.all_of<component::Instance>(e)) {
      return e;
    }
    e = reg.get<component::Node>(e).parent;
  }
  return entt::entity{entt::null};
}

/* Place the feet on the ground.
 * Runs on the animated pose after the hierarchy update and before SolveIK. */
void SolveFootIK(
  Hierarchy& scene,
  Animator& animator,
  Skin const& skin,
  entt::entity animator_entity,
  GroundQuery const& ground,
  float dt
) {
  auto& reg = scene.registry;
  auto& rig = *animator.foot;

  WorldJoint instance{};
  if (!ground || !ReadWorld(reg, FindInstanceRoot(reg, animator_entity), instance)) {
    return;
  }

  float const k = (rig.params.smooth > 0.0f) ? (1.0f - std::exp(-rig.params.smooth * dt)) : 1.0f;
  float const animated_floor = instance.position.y + rig.params.ankle_height;

  float drop = 0.0f;

  for (auto& foot : rig.feet) {
    auto* chain = FindChain(animator, foot.chain);
    if (!chain) {
      continue;
    }

    WorldJoint end{};
    if (!ReadWorld(reg, JointEntity(reg, skin, chain->end), end)) {
      continue;
    }
    vec3 const p = end.position;
    vec3 const pp = vec3(p.x, p.y + rig.params.probe_height, p.z);

    // Calculate how far is the floor from what the animation expects.
    float floor_y = -std::numeric_limits<float>::infinity();
    float delta = 0.0f;
    vec3 normal{0.0f, 1.0f, 0.0f};
    GroundHit hit{};
    if (ground(pp, rig.params.probe_height + rig.params.probe_depth, hit)) {
      floor_y = hit.position.y + rig.params.ankle_height;
      delta = floor_y - animated_floor;
      normal = hit.normal;
    }
    foot.offset += (delta - foot.offset) * k;
    foot.normal = lina::normalize(foot.normal + (normal - foot.normal) * k);

    // A foot follows the ground while it is planted, and is free once lifted.
    float const lift = (p.y - animated_floor) / rig.params.blend_height;
    float const planted = 1.0f - std::clamp(lift, 0.0f, 1.0f);
    foot.planted = planted;

    float const y = std::max(p.y + foot.offset * planted, floor_y);
    chain->target = vec3(p.x, y, p.z);

    // Only feet pushed below the animation pull the pelvis down.
    drop = std::min(drop, (y - p.y) * std::min(chain->applied_weight, 1.0f));
  }

  rig.pelvis_offset += (drop - rig.pelvis_offset) * k;
  if (std::abs(rig.pelvis_offset) < 1.0e-5f) {
    return;
  }

  auto const e_pelvis = JointEntity(reg, skin, rig.pelvis);
  auto* transform = (e_pelvis != entt::null) ? reg.try_get<component::Transform>(e_pelvis) : nullptr;
  if (!transform) {
    return;
  }

  // World space offset to the local space of the pelvis' parent.
  vec3 offset{0.0f, rig.pelvis_offset, 0.0f};
  if (auto const parent = reg.get<component::Node>(e_pelvis).parent; parent != entt::null) {
    if (auto const* global = reg.try_get<component::GlobalTransform>(parent)) {
      offset = lina::mul(lina::inverse(lina::to_mat3(global->worldMatrix)), offset);
    }
  }
  transform->translation += offset;
  scene.updateSubtree(e_pelvis);
}

/* Rotate each foot about its ankle so its sole follows the ground normal.
 * Runs after SolveIK, the foot's world rotation depends on the solved leg. */
void AlignFeet(Hierarchy& scene, Animator& animator, Skin const& skin) {
  auto& reg = scene.registry;
  auto const& rig = *animator.foot;
  if (!rig.params.align) {
    return;
  }

  for (auto const& foot : rig.feet) {
    auto const* chain = FindChain(animator, foot.chain);
    if (!chain) {
      continue;
    }
    float const s = foot.planted * std::min(chain->applied_weight, 1.0f);
    if (s <= 1.0e-4f) {
      continue;
    }

    auto const e_end = JointEntity(reg, skin, chain->end);
    WorldJoint end{};
    if (!ReadWorld(reg, e_end, end)) {
      continue;
    }
    auto* transform = reg.try_get<component::Transform>(e_end);
    if (!transform) {
      continue;
    }

    // World space tilt from the up axis to the ground normal, limited and weighted.
    quat delta = lina::shortest_arc(vec3(0.0f, 1.0f, 0.0f), foot.normal);
    if (lina::qangle(delta) > rig.params.max_angle) {
      delta = lina::rotation_quat(lina::qaxis(delta), rig.params.max_angle);
    }
    delta = lina::qlerp(quat(0.0f, 0.0f, 0.0f, 1.0f), delta, s);

    quat parent_rotation{0.0f, 0.0f, 0.0f, 1.0f};
    if (auto const parent = reg.get<component::Node>(e_end).parent; parent != entt::null) {
      WorldJoint p{};
      if (ReadWorld(reg, parent, p)) {
        parent_rotation = p.rotation;
      }
    }

    transform->rotation = lina::normalize(
      lina::qmul(lina::qinv(parent_rotation), delta, end.rotation)
    );
    scene.updateSubtree(e_end);
  }
}

// ----------------------------

void TickLook(Animator& animator, float dt) {
  for (auto& look : animator.look) {
    StepWeight(look.applied_weight, look.params.weight, look.fade_rate, dt);
  }
}

template<typename Fn>
uint32_t ForEachLook(entt::registry& reg, entt::entity instance_root, LookId id, Fn&& fn) {
  uint32_t count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    for (auto& look : animator.look) {
      if (look.id == id.index) {
        fn(look);
        ++count;
      }
    }
  });
  return count;
}

/* Rotate a joint about its own origin by a world space delta, and refresh its subtree. */
bool RotateJointWorld(Hierarchy& scene, entt::entity e, quat const& delta) {
  auto& reg = scene.registry;

  WorldJoint joint{};
  if (!ReadWorld(reg, e, joint)) {
    return false;
  }
  auto* transform = reg.try_get<component::Transform>(e);
  if (!transform) {
    return false;
  }

  quat parent_rotation{0.0f, 0.0f, 0.0f, 1.0f};
  if (auto const parent = reg.get<component::Node>(e).parent; parent != entt::null) {
    WorldJoint p{};
    if (ReadWorld(reg, parent, p)) {
      parent_rotation = p.rotation;
    }
  }

  transform->rotation = lina::normalize(
    lina::qmul(lina::qinv(parent_rotation), delta, joint.rotation)
  );
  scene.updateSubtree(e);
  return true;
}

[[nodiscard]]
std::optional<LookAt> ResolveLookAt(
  Skeleton const& skeleton,
  AnimationSystem::LookAtDesc const& desc
) {
  uint32_t const head = skeleton.findJoint(std::string{desc.head});
  if (head == kInvalidIndexU32) {
    return std::nullopt;
  }

  uint32_t const length = std::max(desc.length, 1u);
  std::vector<uint32_t> joints{head};
  for (uint32_t i = 1u; i < length; ++i) {
    auto const parent = ParentOf(skeleton, joints.back());
    if (parent == kInvalidIndexU32) {
      LOGW("[Animation] look-at: \"{}\" has fewer than {} joint(s) in its chain.", desc.head, length);
      return std::nullopt;
    }
    joints.push_back(parent);
  }
  std::ranges::reverse(joints);   // root-most first

  LookAt look{};

  // Linear ramp toward the head (1, 2, 3 ... normalized).
  float const total = 0.5f * static_cast<float>(length) * static_cast<float>(length + 1u);
  for (uint32_t i = 0u; i < length; ++i) {
    look.links.push_back({ .joint = joints[i], .weight = static_cast<float>(i + 1u) / total });
  }

  auto const &dp = desc.params;
  auto & lp = look.params;

  float const aim_length = lina::length(dp.aim);
  lp.aim              = (aim_length > lina::kEpsilon) ? (dp.aim / aim_length)
                                                      : vec3(0.0f, 0.0f, 1.0f);
  lp.weight           = std::clamp(dp.weight, 0.0f, 1.0f);
  lp.max_angle        = std::clamp(dp.max_angle, 0.0f, 120.0f) * (lina::kPi / 180.0f);
  lp.fade_angle       = std::max(dp.fade_angle, 1.0f) * (lina::kPi / 180.0f);
  lp.smooth           = std::max(dp.smooth, 0.0f);
  look.applied_weight = lp.weight;
  return look;
}

/* Aim the look-ats of an animator: runs before SolveIK so arm targets stay put. */
void SolveLook(Hierarchy& scene, Animator& animator, Skin const& skin, float dt) {
  auto& reg = scene.registry;
  quat const identity{lina::identity};

  for (auto& look : animator.look) {
    if ((look.applied_weight <= 0.0f) || look.links.empty()) {
      // restart from the animation when it comes back
      look.has_direction = false;
      continue;
    }

    WorldJoint head{};
    if (!ReadWorld(reg, JointEntity(reg, skin, look.links.back().joint), head)) {
      continue;
    }

    vec3 const to_target = look.target - head.position;
    float const distance = lina::length(to_target);
    if (distance < lina::kEpsilon) {
      continue;
    }

    // Where the head looks in the animation, and where it should look.
    vec3 const aim = lina::normalize(lina::qrot(head.rotation, look.params.aim));
    vec3 const new_aim = LimitLookDirection(
      aim, to_target / distance, look.params.max_angle, look.params.fade_angle
    );

    // Smooth the world direction (the head keeps tracking while the body animates).
    float const k = (look.params.smooth > 0.0f)
      ? (1.0f - std::exp(-look.params.smooth * dt)) : 1.0f;
    if (!look.has_direction) {
      look.direction = aim;
      look.has_direction = true;
    }
    vec3 const blended = look.direction + (new_aim - look.direction) * k;
    look.direction = (lina::length(blended) > 1.0e-4f) ? lina::normalize(blended) : new_aim;

    quat const total = lina::qlerp(
      identity,
      lina::shortest_arc(aim, look.direction),
      std::min(look.applied_weight, 1.0f)
    );

    // Root to tip. The parts share an axis, so their angles add up to the full swing.
    for (auto const& link : look.links) {
      RotateJointWorld(
        scene,
        JointEntity(reg, skin, link.joint),
        lina::qlerp(identity, total, link.weight)
      );
    }
  }
}

// ----------------------------

/* Returns the index of the first valid transition, or kInvalidIndexU32. */
[[nodiscard]]
uint32_t FindTransition(Animator const& animator, Skeleton const& skeleton) {
  auto const& transitions = animator.graph->transitions;

  for (uint32_t i = 0u; i < transitions.size(); ++i) {
    auto const& transition = transitions[i];

    bool const any_state = !transition.from.valid();
    if (any_state ? (transition.to == animator.state) : (transition.from != animator.state)) {
      continue;
    }
    if (transition.on_finish && !animator.finished) {
      continue;
    }
    if (transition.exit_time >= 0.0f) {
      auto const nt = animator.root ? animator.root->normalizedTime() : 1.0f;
      if (nt < transition.exit_time) {
        continue;
      }
    }

    bool holds = true;
    for (auto const& condition : transition.conditions) {
      using Op = AnimationStateMachine::Condition::Op;

      float const value = ReadParameter(animator, condition.parameter);
      bool satisfied = false;
      switch (condition.op) {
        case Op::Greater: satisfied = (value > condition.value); break;
        case Op::Less:    satisfied = (value < condition.value); break;
        case Op::Trigger: satisfied = (value > 0.0f);            break;
      }
      holds = holds && satisfied;
    }
    if (holds) {
      return i;
    }
  }
  return kInvalidIndexU32;
}

void UpdateStateMachine(
  Animator& animator,
  PoseCache& cache,
  Skeleton const& skeleton
) {
  if (!animator.graph
   || !animator.state.valid()
   || !(animator.playing || animator.finished)) {
    return;
  }
  auto const i = FindTransition(animator, skeleton);
  if (i == kInvalidIndexU32) {
    return;
  }

  auto const& graph = *animator.graph;
  auto const& transition = graph.transitions[i];

  // when the transition fires it consumes the triggers it was waiting for.
  for (auto const& condition : transition.conditions) {
    if ((condition.op == AnimationStateMachine::Condition::Op::Trigger)
     && (condition.parameter.index < animator.parameters.size())) {
      animator.parameters[condition.parameter.index] = 0.0f;
    }
  }

  LOGD("[Animation] \"{}\": {} -> {}.",
    graph.name,
    graph.states[animator.state.index].name,
    graph.states[transition.to.index].name
  );
  EnterState(animator, cache, skeleton, transition.to, transition.fade);
}

} // namespace

/* -------------------------------------------------------------------------- */

void AnimationSystem::preUpdate(float elapsed_time) {
  float const dt = last_elapsed_time_
                 ? std::clamp(elapsed_time - *last_elapsed_time_, 0.0f, kMaxDeltaTime)
                 : 0.0f;
  last_elapsed_time_ = elapsed_time;

  advance(dt);
}

// ----------------------------------------------------------------------------

void AnimationSystem::advance(float dt) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;
  last_dt_ = dt;

  reg.view<Animator, Skin>().each(
    [&](Animator& animator, Skin const& skin) {
      if (!skin.skeleton) {
        return;
      }
      auto const& skeleton = *skin.skeleton;

      TickTriggers(animator, dt);
      TickIK(animator, dt);
      TickChain(animator, dt);
      TickLook(animator, dt);

      UpdateStateMachine(animator, poses_, skeleton);

      // An animator with IK, look-at or chains is resampled every frame, even paused,
      // so the solvers always start from the clean animated pose.
      if (!animator.playing
       && !animator.resample
       && animator.ik.empty()
       && animator.chain.empty()
       && animator.look.empty()) {
        return;
      }
      if (!animator.root) {
        animator.playing = false;
        animator.resample = false;
        return;
      }

      animator.root->evaluate({
        .skeleton   = skeleton,
        .cache      = poses_,
        .parameters = animator.parameters,
        .dt         = dt,
        .delta      = dt * animator.speed,
        .reverse    = animator.speed < 0.0f,
        .playing    = animator.playing,
      }, animator.pose);

      animator.resample = false;
      ApplyPose(reg, skin, animator.pose);

      // Once a cross-fade is over, keep only the motion.
      if (auto child = animator.root->takeChildIfDone()) {
        animator.root = std::move(child);
      }

      if (animator.playing && animator.root->finished()) {
        animator.playing = false;
        animator.finished = true;
        LOGD("[Animation] clip \"{}\" finished.", animator.root->name());
      }
    }
  );
}

// ----------------------------------------------------------------------------

void AnimationSystem::postUpdate() {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  // Solve IK.
  reg.view<Animator, Skin>().each(
    [&](entt::entity e, Animator& animator, Skin const& skin) {
      if (!animator.root
       || (animator.ik.empty() && animator.look.empty() && animator.chain.empty())) {
        return;
      }
      if (animator.foot) {
        SolveFootIK(*scene_, animator, skin, e, ground_query_, last_dt_);
      }
      SolveLook(*scene_, animator, skin, last_dt_);
      SolveIK(*scene_, animator, skin);
      SolveChainIK(*scene_, animator, skin);
      if (animator.foot) {
        AlignFeet(*scene_, animator, skin);
      }
    }
  );

  reg.view<Skin, component::GlobalTransform>().each(
    [&](Skin& skin, component::GlobalTransform const& mesh_global) {
      if (!skin.skeleton) {
        return;
      }
      auto const& inverse_binds = skin.skeleton->inverse_bind_matrices;
      size_t const count = std::min({
        skin.joints.size(), skin.matrices.size(), inverse_binds.size()
      });

      // The mesh node is drawn with its own global matrix, so it is cancelled here (glTF).
      mat4 const inverse_mesh = lina::inverse(mesh_global.worldMatrix);

      bool const dqs = (skin.mode == SkinningMode::DualQuaternion);

      for (size_t j = 0; j < count; ++j) {
        auto const joint = skin.joints[j];
        auto const* joint_global = (joint != entt::null && reg.valid(joint))
                                 ? reg.try_get<component::GlobalTransform>(joint)
                                 : nullptr;

        mat4 const m = joint_global
          ? lina::mul(lina::mul(inverse_mesh, joint_global->worldMatrix), inverse_binds[j])
          : mat4{lina::identity};

        skin.matrices[j] = dqs ? PackDualQuat(lina::dual_quat_from_matrix(m)) : m;
      }
      skin.palette_mode = skin.mode;
    }
  );
}

/* -------------------------------------------------------------------------- */

bool AnimationSystem::play(
  entt::entity instance_root,
  std::string_view clip_name,
  bool loop,
  float fade_duration
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] play: entity is not an instance root.");
    return false;
  }

  uint32_t animator_count = 0u;
  uint32_t played_count = 0u;

  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    ++animator_count;
    if (!skin.skeleton) {
      return;
    }
    auto const clip = skin.skeleton->findClip(clip_name);
    if (clip == kInvalidIndexU32) {
      return;
    }

    StartClip(animator, poses_, *skin.skeleton, clip, loop, fade_duration);

    animator.state = StateId{};  // reset to manual control
    ++played_count;
  });

  if (played_count == 0u) {
    LOGW("[Animation] no clip \"{}\" in this instance ({} animator(s)).", clip_name, animator_count);
    return false;
  }
  LOGD("[Animation] play \"{}\" on {}/{} animator(s).", clip_name, played_count, animator_count);
  return true;
}

// ----------------------------------------------------------------------------

void AnimationSystem::pause(entt::entity instance_root) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] pause: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [](Animator& animator, Skin&) {
    animator.playing = false;
  });
}

// ----------------------------------------------------------------------------

void AnimationSystem::resume(entt::entity instance_root) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] resume: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [](Animator& animator, Skin&) {
    if (!animator.root) {
      return;
    }
    animator.root->onResume(animator.speed < 0.0f);
    animator.finished = false;
    animator.playing = true;
  });
}

// ----------------------------------------------------------------------------

void AnimationSystem::stop(entt::entity instance_root) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] stop: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    animator.root.reset();
    animator.finished = false;
    animator.playing = false;
    animator.resample = false;
    animator.state = StateId{};  // reset to manual control

    // (rest_pose is empty for skeletons without animations as they never moved.)
    if (skin.skeleton && !skin.skeleton->rest_pose.joints.empty()) {
      animator.pose = skin.skeleton->rest_pose;
      ApplyPose(reg, skin, animator.pose);
    }
  });
  LOGD("[Animation] stopped.");
}

// ----------------------------------------------------------------------------

std::vector<std::string> AnimationSystem::jointNames(entt::entity instance_root) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  std::vector<std::string> names{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return names;
  }
  ForEachAnimator(reg, instance_root, [&](Animator&, Skin& skin) {
    if (!names.empty() || !skin.skeleton) {
      return;
    }
    names = skin.skeleton->names;
  });
  return names;
}

// ----------------------------------------------------------------------------

std::vector<std::string> AnimationSystem::clipNames(entt::entity instance_root) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  std::vector<std::string> names{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return names;
  }
  ForEachAnimator(reg, instance_root, [&](Animator&, Skin& skin) {
    if (!skin.skeleton) {
      return;
    }
    for (auto const& clip : skin.skeleton->clips) {
      if (std::ranges::find(names, clip.name) == names.end()) {
        names.push_back(clip.name);
      }
    }
  });
  return names;
}

// ----------------------------------------------------------------------------

AnimationSystem::State AnimationSystem::state(entt::entity instance_root) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  State result{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return result;
  }
  bool found = false;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (found || !animator.root) {
      return;
    }
    found = true;
    result = {
      .clip     = animator.root->name(),
      .time     = animator.root->time(),
      .duration = animator.root->duration(),
      .speed    = animator.speed,
      .fade     = animator.root->fadeProgress(),
      .loop     = animator.root->looping(),
      .playing  = animator.playing,
    };
    if (animator.graph && animator.state.valid()) {
      result.machine_state = animator.graph->states[animator.state.index].name;
    }
  });
  return result;
}

// ----------------------------------------------------------------------------

void AnimationSystem::setTime(entt::entity instance_root, float time) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setTime: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [time](Animator& animator, Skin&) {
    if (!animator.root) {
      return;
    }
    animator.root->seek(time);
    animator.resample = true;
  });
}

// ----------------------------------------------------------------------------

void AnimationSystem::setSpeed(entt::entity instance_root, float speed) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setSpeed: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [speed](Animator& animator, Skin&) {
    animator.speed = speed;
  });
}

// ----------------------------------------------------------------------------

void AnimationSystem::setLoop(entt::entity instance_root, bool loop) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setLoop: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [loop](Animator& animator, Skin&) {
    if (animator.root) {
      animator.root->setLoop(loop);
    }
  });
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

bool AnimationSystem::setGraph(
  entt::entity instance_root,
  std::shared_ptr<AnimationGraphDesc const> graph
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setGraph: entity is not an instance root.");
    return false;
  }
  if (!graph) {
    LOGE("[Animation] setGraph: null graph.");
    return false;
  }

  uint32_t set_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (!skin.skeleton) {
      return;
    }
    auto compiled = acquireGraph(graph, *skin.skeleton);
    if (!compiled) {
      return;
    }

    animator.root.reset();
    animator.finished   = false;
    animator.playing    = false;
    animator.resample   = false;
    animator.graph      = std::move(compiled);
    animator.state      = StateId{};

    animator.parameters.clear();
    for (auto const& param : animator.graph->parameters) {
      animator.parameters.push_back(param.default_value);
    }

    if (animator.graph->initial.valid()) {
      EnterState(animator, poses_, *skin.skeleton, animator.graph->initial, 0.0f);
    }
    ++set_count;
  });
  return set_count > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::playBlendTree(
  entt::entity instance_root,
  std::string_view tree_name,
  float fade_duration
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] playBlendTree: entity is not an instance root.");
    return false;
  }

  uint32_t played_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!animator.graph) {
      return;
    }
    auto const tree = animator.graph->findTree(tree_name);
    if (tree == kInvalidIndexU32) {
      return;
    }
    StartBlendTree(animator, poses_, tree, fade_duration);
    animator.state = StateId{};
    ++played_count;
  });

  if (played_count == 0u) {
    LOGW("[Animation] no blend tree \"{}\" in this instance.", tree_name);
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

ParamId AnimationSystem::findParameter(entt::entity instance_root, std::string_view name) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  ParamId id{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return id;
  }
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!id.valid() && animator.graph) {
      id = animator.graph->findParameter(name);
    }
  });
  return id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setParameter(entt::entity instance_root, ParamId id, float value) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setParameter: entity is not an instance root.");
    return false;
  }

  uint32_t set_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (id.valid() && id.index < animator.parameters.size()) {
      animator.parameters[id.index] = value;
      ++set_count;
    }
  });
  return set_count > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setParameter(
  entt::entity instance_root,
  std::string_view name,
  float value
) {
  auto const id = findParameter(instance_root, name);
  if (!id.valid()) {
    LOGW("[Animation] no parameter \"{}\" in this instance.", name);
    return false;
  }
  return setParameter(instance_root, id, value);
}

// ----------------------------------------------------------------------------

float AnimationSystem::parameter(entt::entity instance_root, ParamId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  float value = 0.0f;
  if (!IsInstanceRoot(reg, instance_root)) {
    return value;
  }
  bool found = false;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!found) {
      found = true;
      value = ReadParameter(animator, id);
    }
  });
  return value;
}

// ----------------------------------------------------------------------------

float AnimationSystem::parameter(entt::entity instance_root, std::string_view name) const {
  return parameter(instance_root, findParameter(instance_root, name));
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setTrigger(entt::entity instance_root, ParamId id) {
  LOG_CHECK(scene_ != nullptr);
  return WriteTrigger(scene_->registry, instance_root, id, true) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setTrigger(entt::entity instance_root, std::string_view name) {
  return setTrigger(instance_root, findParameter(instance_root, name));
}

// ----------------------------------------------------------------------------

bool AnimationSystem::resetTrigger(entt::entity instance_root, ParamId id) {
  LOG_CHECK(scene_ != nullptr);
  return WriteTrigger(scene_->registry, instance_root, id, false) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::resetTrigger(entt::entity instance_root, std::string_view name) {
  return resetTrigger(instance_root, findParameter(instance_root, name));
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

StateId AnimationSystem::findState(entt::entity instance_root, std::string_view name) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  StateId id{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return id;
  }
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!id.valid() && animator.graph) {
      id = animator.graph->findState(name);
    }
  });
  return id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setState(
  entt::entity instance_root,
  StateId state,
  float fade_duration
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setState: entity is not an instance root.");
    return false;
  }

  uint32_t entered_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (!skin.skeleton || !animator.graph || !state.valid()
     || state.index >= animator.graph->states.size()) {
      return;
    }
    EnterState(animator, poses_, *skin.skeleton, state, fade_duration);
    ++entered_count;
  });

  if (entered_count == 0u) {
    LOGW("[Animation] invalid state for this instance.");
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setState(
  entt::entity instance_root,
  std::string_view state_name,
  float fade_duration
) {
  auto const state = findState(instance_root, state_name);
  if (!state.valid()) {
    LOGW("[Animation] no state \"{}\" in this instance.", state_name);
    return false;
  }
  return setState(instance_root, state, fade_duration);
}

// ----------------------------------------------------------------------------

std::vector<std::string> AnimationSystem::stateNames(entt::entity instance_root) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  std::vector<std::string> names{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return names;
  }
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!names.empty() || !animator.graph) {
      return;
    }
    for (auto const& state : animator.graph->states) {
      names.push_back(state.name);
    }
  });
  return names;
}

// ----------------------------------------------------------------------------

std::shared_ptr<AnimationGraph const> AnimationSystem::acquireGraph(
  std::shared_ptr<AnimationGraphDesc const> const& desc,
  Skeleton const& skeleton
) {
  GraphKey const key{desc.get(), &skeleton};

  if (auto const it = graph_cache_.find(key); it != graph_cache_.end()) {
    if (auto graph = it->second.lock()) {
      return graph;
    }
  }

  auto graph = CompileAnimationGraph(desc, skeleton);
  if (graph) {
    // drop the previous graph when unused
    std::erase_if(graph_cache_, [](auto const& entry) {
      return entry.second.expired();
    });
    // cache the new one.
    graph_cache_[key] = graph;
  }
  return graph;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

IKId AnimationSystem::addTwoBoneIK(entt::entity instance_root, TwoBoneIKDesc const& desc) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] addTwoBoneIK: entity is not an instance root.");
    return {};
  }

  IKId const id{next_ik_id_};
  uint32_t added_count = 0u;

  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (!skin.skeleton) {
      return;
    }
    auto chain = ResolveTwoBoneIK(*skin.skeleton, desc);
    if (!chain || (std::max({chain->root, chain->mid, chain->end}) >= skin.joints.size())) {
      return;
    }

    chain->id = id.index;
    chain->weight = std::clamp(desc.weight, 0.0f, 1.0f);
    chain->applied_weight = chain->weight;

    // start on the current end position.
    WorldJoint end{};
    if (ReadWorld(reg, JointEntity(reg, skin, chain->end), end)) {
      chain->target = end.position;
    }

    // the pole starts on the mid joint, which keeps the animated bend plane.
    WorldJoint mid{};
    if (ReadWorld(reg, JointEntity(reg, skin, chain->mid), mid)) {
      chain->pole = mid.position;
    }

    animator.ik.push_back(*chain);
    ++added_count;
  });

  if (added_count == 0u) {
    LOGW("[Animation] no IK chain ending at \"{}\" in this instance.", desc.end);
    return {};
  }
  ++next_ik_id_;
  return id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::removeIK(entt::entity instance_root, IKId id) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] removeIK: entity is not an instance root.");
    return false;
  }

  uint32_t removed_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    auto const count = std::erase_if(animator.ik, [&](auto const& chain) {
      return chain.id == id.index;
    });
    if (count > 0u) {
      removed_count += static_cast<uint32_t>(count);
      animator.resample = true;
    }
  });
  return removed_count > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setIKTarget(entt::entity instance_root, IKId id, vec3 const& world_position) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setIKTarget: entity is not an instance root.");
    return false;
  }
  return ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    chain.target = world_position;
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setIKWeight(
  entt::entity instance_root,
  IKId id,
  float weight,
  float blend_seconds
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setIKWeight: entity is not an instance root.");
    return false;
  }

  float const target = std::clamp(weight, 0.0f, 1.0f);

  return ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    if (blend_seconds > 0.0f) {
      if (chain.weight == target) {
        return;
      }
      chain.weight = target;
      chain.fade_rate = std::abs(target - chain.applied_weight) / blend_seconds;
    } else {
      chain.weight = target;
      chain.applied_weight = target;
      chain.fade_rate = 0.0f;
    }
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setIKPole(entt::entity instance_root, IKId id, vec3 const& world_position) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setIKPole: entity is not an instance root.");
    return false;
  }
  return ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    chain.pole = world_position;
    chain.use_pole = true;
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::clearIKPole(entt::entity instance_root, IKId id) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] clearIKPole: entity is not an instance root.");
    return false;
  }
  return ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    chain.use_pole = false;
  }) > 0u;
}

// ----------------------------------------------------------------------------

float AnimationSystem::ikWeight(entt::entity instance_root, IKId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  float weight = 0.0f;
  if (!IsInstanceRoot(reg, instance_root)) {
    return weight;
  }
  bool found = false;
  ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    if (!found) {
      found = true;
      weight = chain.applied_weight;
    }
  });
  return weight;
}

// ----------------------------------------------------------------------------

vec3 AnimationSystem::ikPole(entt::entity instance_root, IKId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  vec3 pole{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return pole;
  }
  bool found = false;
  ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    if (!found) {
      found = true;
      pole = chain.pole;
    }
  });
  return pole;
}

// ----------------------------------------------------------------------------

vec3 AnimationSystem::ikTarget(entt::entity instance_root, IKId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  vec3 target{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return target;
  }
  bool found = false;
  ForEachIK(reg, instance_root, id, [&](TwoBoneIK& chain) {
    if (!found) {
      found = true;
      target = chain.target;
    }
  });
  return target;
}

// ----------------------------------------------------------------------------

std::vector<std::string> AnimationSystem::twoBoneIKEndJoints(entt::entity instance_root) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  std::vector<std::string> names{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return names;
  }
  ForEachAnimator(reg, instance_root, [&](Animator&, Skin& skin) {
    if (!names.empty() || !skin.skeleton) {
      return;
    }
    auto const& skeleton = *skin.skeleton;
    for (uint32_t j = 0u; j < skeleton.names.size(); ++j) {
      if (ParentOf(skeleton, ParentOf(skeleton, j)) != kInvalidIndexU32) {
        names.push_back(skeleton.names[j]);
      }
    }
  });
  return names;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

ChainIKId AnimationSystem::addChainIK(entt::entity instance_root, ChainIKDesc const& desc) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] addChainIK: entity is not an instance root.");
    return {};
  }

  ChainIKId const id{next_chain_id_};
  uint32_t added_count = 0u;

  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (!skin.skeleton) {
      return;
    }
    auto chain = ResolveChainIK(*skin.skeleton, desc);
    if (!chain || std::ranges::any_of(chain->joints, [&](uint32_t j) {
          return j >= skin.joints.size();
        })) {
      return;
    }
    chain->id = id.index;

    // start on the current end position.
    WorldJoint end{};
    if (ReadWorld(reg, JointEntity(reg, skin, chain->joints.back()), end)) {
      chain->target = end.position;
    }

    // the pole starts on a middle joint, which keeps the animated bend.
    WorldJoint mid{};
    if (ReadWorld(reg, JointEntity(reg, skin, chain->joints[chain->joints.size() / 2u]), mid)) {
      chain->pole = mid.position;
    }

    animator.chain.push_back(std::move(*chain));
    ++added_count;
  });

  if (added_count == 0u) {
    LOGW("[Animation] no chain IK ending at \"{}\" in this instance.", desc.end);
    return {};
  }
  ++next_chain_id_;
  return id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::removeChainIK(entt::entity instance_root, ChainIKId id) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] removeChainIK: entity is not an instance root.");
    return false;
  }

  uint32_t removed_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    auto const count = std::erase_if(animator.chain, [&](auto const& chain) {
      return chain.id == id.index;
    });
    if (count > 0u) {
      removed_count += static_cast<uint32_t>(count);
      animator.resample = true;
    }
  });
  return removed_count > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setChainIKTarget(
  entt::entity instance_root,
  ChainIKId id,
  vec3 const& world_position
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setChainIKTarget: entity is not an instance root.");
    return false;
  }
  return ForEachChain(reg, instance_root, id, [&](ChainIK& chain) {
    chain.target = world_position;
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setChainIKWeight(
  entt::entity instance_root,
  ChainIKId id,
  float weight,
  float blend_seconds
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setChainIKWeight: entity is not an instance root.");
    return false;
  }

  float const target = std::clamp(weight, 0.0f, 1.0f);
  return ForEachChain(reg, instance_root, id, [&](ChainIK& chain) {
    SetFade(chain.weight, chain.applied_weight, chain.fade_rate, target, blend_seconds);
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setChainIKPole(
  entt::entity instance_root,
  ChainIKId id,
  vec3 const& world_position
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setChainIKPole: entity is not an instance root.");
    return false;
  }
  return ForEachChain(reg, instance_root, id, [&](ChainIK& chain) {
    chain.pole = world_position;
    chain.use_pole = true;
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::clearChainIKPole(entt::entity instance_root, ChainIKId id) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] clearChainIKPole: entity is not an instance root.");
    return false;
  }
  return ForEachChain(reg, instance_root, id, [&](ChainIK& chain) {
    chain.use_pole = false;
  }) > 0u;
}

// ----------------------------------------------------------------------------

vec3 AnimationSystem::chainIKTarget(entt::entity instance_root, ChainIKId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  vec3 target{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return target;
  }
  bool found = false;
  ForEachChain(reg, instance_root, id, [&](ChainIK& chain) {
    if (!found) {
      found = true;
      target = chain.target;
    }
  });
  return target;
}

// ----------------------------------------------------------------------------

float AnimationSystem::chainIKWeight(entt::entity instance_root, ChainIKId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  float weight = 0.0f;
  if (!IsInstanceRoot(reg, instance_root)) {
    return weight;
  }
  bool found = false;
  ForEachChain(reg, instance_root, id, [&](ChainIK& chain) {
    if (!found) {
      found = true;
      weight = chain.applied_weight;
    }
  });
  return weight;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void AnimationSystem::setGroundQuery(GroundQuery query) {
  ground_query_ = std::move(query);
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setFootIK(entt::entity instance_root, FootIKDesc const& desc) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setFootIK: entity is not an instance root.");
    return false;
  }
  if (desc.feet.empty()) {
    LOGE("[Animation] setFootIK: no foot chain.");
    return false;
  }

  uint32_t set_count = 0u;

  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (!skin.skeleton) {
      return;
    }
    auto const& skeleton = *skin.skeleton;

    FootRig rig{};
    for (auto const id : desc.feet) {
      if (!FindChain(animator, id.index)) {
        return;
      }
      rig.feet.push_back({ .chain = id.index });
    }
    auto const& first = *FindChain(animator, rig.feet.front().chain);

    rig.pelvis = desc.pelvis.empty() ? ParentOf(skeleton, first.root)
                                     : skeleton.findJoint(std::string{desc.pelvis});
    if ((rig.pelvis == kInvalidIndexU32) || (rig.pelvis >= skin.joints.size())) {
      LOGW("[Animation] foot IK: invalid pelvis joint.");
      return;
    }
    for (auto const& foot : rig.feet) {
      if (!IsAncestor(skeleton, rig.pelvis, FindChain(animator, foot.chain)->root)) {
        LOGW("[Animation] foot IK: the pelvis is not an ancestor of every chain.");
        return;
      }
    }

    auto const& dp = desc.params;
    auto &rp       = rig.params;

    rp.probe_height  = std::max(dp.probe_height, 0.0f);
    rp.probe_depth   = std::max(dp.probe_depth, 0.0f);
    rp.blend_height  = std::max(dp.blend_height, 1.0e-3f);
    rp.smooth        = std::max(dp.smooth, 0.0f);
    rp.align         = dp.align;
    rp.max_angle     = lina::radians(std::clamp(dp.max_angle, 0.0f, 90.0f));

    if (dp.ankle_height >= 0.0f) {
      rp.ankle_height = dp.ankle_height;
    } else {
      WorldJoint end{}, base{};
      if (ReadWorld(reg, JointEntity(reg, skin, first.end), end)
       && ReadWorld(reg, instance_root, base)) {
        rp.ankle_height = std::max(end.position.y - base.position.y, 0.0f);
      }
    }

    if (animator.foot) {
      rig.pelvis_offset = animator.foot->pelvis_offset;
      for (auto& foot : rig.feet) {
        for (auto const& old : animator.foot->feet) {
          if (old.chain == foot.chain) {
            foot.offset = old.offset;
            foot.normal = old.normal;
          }
        }
      }
    }

    animator.foot = std::move(rig);
    ++set_count;
  });

  if (set_count == 0u) {
    LOGW("[Animation] foot IK: no animator could use this rig.");
  }
  return set_count > 0u;
}

// ----------------------------------------------------------------------------

void AnimationSystem::clearFootIK(entt::entity instance_root) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] clearFootIK: entity is not an instance root.");
    return;
  }
  ForEachAnimator(reg, instance_root, [](Animator& animator, Skin&) {
    if (animator.foot) {
      animator.foot.reset();
      animator.resample = true;
    }
  });
}

// ----------------------------------------------------------------------------

float AnimationSystem::footIKAnkleHeight(entt::entity instance_root) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  float height = 0.0f;
  if (!IsInstanceRoot(reg, instance_root)) {
    return height;
  }
  bool found = false;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    if (!found && animator.foot) {
      found = true;
      height = animator.foot->params.ankle_height;
    }
  });
  return height;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

LookId AnimationSystem::addLookAt(entt::entity instance_root, LookAtDesc const& desc) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] addLookAt: entity is not an instance root.");
    return {};
  }

  LookId const id{next_look_id_};
  uint32_t added_count = 0u;

  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (!skin.skeleton) {
      return;
    }
    auto look = ResolveLookAt(*skin.skeleton, desc);
    if (!look) {
      return;
    }
    for (auto const& link : look->links) {
      if (link.joint >= skin.joints.size()) {
        return;
      }
    }
    look->id = id.index;

    // start straight ahead of the head.
    WorldJoint head{};
    if (ReadWorld(reg, JointEntity(reg, skin, look->links.back().joint), head)) {
      look->target = head.position
                   + lina::normalize(lina::qrot(head.rotation, look->params.aim));
    }

    animator.look.push_back(std::move(*look));
    ++added_count;
  });

  if (added_count == 0u) {
    LOGW("[Animation] no look-at chain for \"{}\" in this instance.", desc.head);
    return {};
  }
  ++next_look_id_;
  return id;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::removeLookAt(entt::entity instance_root, LookId id) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] removeLookAt: entity is not an instance root.");
    return false;
  }

  uint32_t removed_count = 0u;
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin&) {
    auto const count = std::erase_if(animator.look, [&](auto const& look) {
      return look.id == id.index;
    });
    if (count > 0u) {
      animator.resample = true;
      removed_count += static_cast<uint32_t>(count);
    }
  });
  return removed_count > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setLookTarget(entt::entity instance_root, LookId id, vec3 const& world_position) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setLookTarget: entity is not an instance root.");
    return false;
  }
  return ForEachLook(reg, instance_root, id, [&](LookAt& look) {
    look.target = world_position;
  }) > 0u;
}

// ----------------------------------------------------------------------------

bool AnimationSystem::setLookWeight(
  entt::entity instance_root,
  LookId id,
  float weight,
  float blend_seconds
) {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  if (!IsInstanceRoot(reg, instance_root)) {
    LOGE("[Animation] setLookWeight: entity is not an instance root.");
    return false;
  }

  float const target = std::clamp(weight, 0.0f, 1.0f);

  return ForEachLook(reg, instance_root, id, [&](LookAt& look) {
    if (blend_seconds > 0.0f) {
      if (look.params.weight != target) {
        look.fade_rate = std::abs(target - look.applied_weight) / blend_seconds;
      }
    } else {
      look.fade_rate = 0.0f;
      look.applied_weight = target;
    }
    look.params.weight = target;
  }) > 0u;
}

// ----------------------------------------------------------------------------

vec3 AnimationSystem::lookTarget(entt::entity instance_root, LookId id) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  vec3 target{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return target;
  }
  bool found = false;
  ForEachLook(reg, instance_root, id, [&](LookAt& look) {
    if (!found) {
      found = true;
      target = look.target;
    }
  });
  return target;
}

// ----------------------------------------------------------------------------

std::optional<AnimationSystem::LookAtAim> AnimationSystem::lookAim(
  entt::entity instance_root,
  LookId id
) const {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

  std::optional<LookAtAim> result{};
  if (!IsInstanceRoot(reg, instance_root)) {
    return result;
  }
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (result) {
      return;
    }
    for (auto const& look : animator.look) {
      if ((look.id != id.index) || look.links.empty()) {
        continue;
      }
      WorldJoint head{};
      if (ReadWorld(reg, JointEntity(reg, skin, look.links.back().joint), head)) {
        result = LookAtAim{
          .origin = head.position,
          .direction = lina::normalize(lina::qrot(head.rotation, look.params.aim)),
        };
        return;
      }
    }
  });
  return result;
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"
