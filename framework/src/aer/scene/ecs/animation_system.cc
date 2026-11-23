#include "aer/scene/ecs/animation_system.h"

#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

#include "aer/scene/ecs/hierarchy.h"
#include "aer/scene/ecs/animation_components.h"

/* -------------------------------------------------------------------------- */

namespace scene {

namespace {

using component::Animator;
using component::Skin;

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

/* Advance a clip time by `delta`.
 * Returns true when a non looping clip reaches its end in the playback direction. */
bool StepTime(float& time, float delta, float duration, bool loop, bool reverse) {
  time += delta;

  if (loop) {
    if (duration > lina::kTrueEpsilon) {
      time = std::fmod(time, duration);
      if (time < 0.0f) {
        time += duration;
      }
    } else {
      time = 0.0f;
    }
    return false;
  }

  if (reverse ? (time <= 0.0f) : (time >= duration)) {
    time = std::clamp(time, 0.0f, duration);
    return true;
  }
  return false;
}

/* Blend the snapshot into the new sampled pose. */
void ApplyFade(Animator& animator) {
  auto& fade = animator.fade;
  if (fade.duration <= 0.0f) {
    return;
  }
  float const alpha = std::clamp(fade.time / fade.duration, 0.0f, 1.0f);
  if (alpha >= 1.0f) {
    fade.duration = 0.0f;
    fade.from.joints.clear();
    return;
  }
  float const smooth = alpha * alpha * (3.0f - 2.0f * alpha); // smoothstep
  BlendPoses(fade.from, animator.pose, smooth, animator.pose);
}

/* Snapshot the pose on screen to blend from. */
void StartFade(Animator& animator, float fade_duration) {
  auto& fade = animator.fade;
  if (fade_duration > 0.0f && !animator.pose.joints.empty()) {
    fade.from = animator.pose;
    fade.time = 0.0f;
    fade.duration = fade_duration;
  } else {
    fade.duration = 0.0f;
  }
}

void StartClip(
  Animator& animator,
  Skeleton const& skeleton,
  uint32_t clip,
  bool loop,
  float fade_duration
) {
  StartFade(animator, fade_duration);
  animator.clip = clip;
  animator.loop = loop;
  animator.time = (animator.speed < 0.0f) ? skeleton.clips[clip].duration : 0.0f;
  animator.playing = true;
  animator.finished = false;
  animator.resample = false;
  animator.blend.tree_index = kInvalidIndexU32;
}

void StartBlendTree(Animator& animator, uint32_t tree, float fade_duration) {
  StartFade(animator, fade_duration);
  animator.clip = kInvalidIndexU32;
  animator.loop = true;
  animator.playing = true;
  animator.finished = false;
  animator.resample = false;
  animator.blend.tree_index = tree;
  animator.blend.phase = 0.0f;
}

[[nodiscard]]
inline bool HasBlendTree(Animator const& animator) noexcept {
  return (animator.blend.tree_index != kInvalidIndexU32);
}

[[nodiscard]]
bool HasMotion(Animator const& animator, Skeleton const& skeleton) {
  return HasBlendTree(animator)
    ? (animator.graph && animator.blend.tree_index < animator.graph->trees.size())
    : animator.clip < skeleton.clips.size();
}

[[nodiscard]]
std::string_view MotionName(Animator const& animator, Skeleton const& skeleton) {
  return HasBlendTree(animator)
    ? std::string_view(animator.graph->trees[animator.blend.tree_index].name)
    : std::string_view(skeleton.clips[animator.clip].name);
}

[[nodiscard]]
float MotionDuration(Animator const& animator, Skeleton const& skeleton) {
  return HasBlendTree(animator)
    ? animator.blend.duration
    : skeleton.clips[animator.clip].duration;
}

[[nodiscard]]
float ReadParameter(Animator const& animator, ParamId id) {
  return (id.valid() && id.index < animator.parameters.size())
       ? animator.parameters[id.index]
       : 0.0f;
}

/* Advance the phase of a tree and sample its blended pose into animator.pose. */
void StepBlendTree(Animator& animator, Skeleton const& skeleton, float dt) {
  auto &blend = animator.blend;

  auto const& tree = animator.graph->trees[blend.tree_index];
  auto const w = ComputeBlendTreeWeights(tree, ReadParameter(animator, tree.parameter_id));

  auto const& clip_a = skeleton.clips[tree.entries[w.a].clip_index];
  auto const& clip_b = skeleton.clips[tree.entries[w.b].clip_index];

  float const duration = std::max(
    clip_a.duration + (clip_b.duration - clip_a.duration) * w.t,
    lina::kTrueEpsilon
  );

  if (animator.playing) {
    blend.phase += dt * animator.speed / duration;
    blend.phase -= std::floor(blend.phase);   // wrap into [0, 1)
    if (blend.phase >= 1.0f) {
      blend.phase = 0.0f;
    }
  }

  SampleClip(clip_a, blend.phase * clip_a.duration, true, animator.pose);
  if ((w.b != w.a) && (w.t > 0.0f)) {
    SampleClip(clip_b, blend.phase * clip_b.duration, true, blend.scratch);
    BlendPoses(animator.pose, blend.scratch, w.t, animator.pose);
  }

  animator.time = blend.phase * duration;
  blend.duration = duration;
}

// --------------------------------

void EnterState(
  Animator& animator,
  Skeleton const& skeleton,
  StateId state,
  float fade_duration
) {
  animator.state = state;

  auto const& resolved = animator.graph->states[state.index];
  if (resolved.tree != kInvalidIndexU32) {
    StartBlendTree(animator, resolved.tree, fade_duration);
  } else {
    StartClip(animator, skeleton, resolved.clip, resolved.loop, fade_duration);
  }
}

[[nodiscard]]
float NormalizedTime(Animator const& animator, Skeleton const& skeleton) {
  if (HasBlendTree(animator)) {
    return animator.blend.phase;
  }
  float const duration = skeleton.clips[animator.clip].duration;
  return (duration > lina::kTrueEpsilon) ? std::clamp(animator.time / duration, 0.0f, 1.0f) : 1.0f;
}

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
    if ((transition.exit_time >= 0.0f)
     && (NormalizedTime(animator, skeleton) < transition.exit_time)) {
      continue;
    }

    bool holds = true;
    for (auto const& condition : transition.conditions) {
      float const value = ReadParameter(animator, condition.parameter);
      holds = holds && ((condition.op == AnimationStateMachine::Condition::Op::Greater)
                      ? (value > condition.value)
                      : (value < condition.value));
    }
    if (holds) {
      return i;
    }
  }
  return kInvalidIndexU32;
}

void UpdateStateMachine(Animator& animator, Skeleton const& skeleton) {
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
  LOGD("[Animation] \"{}\": {} -> {}.", graph.name,
       graph.states[animator.state.index].name, graph.states[transition.to.index].name);
  EnterState(animator, skeleton, transition.to, transition.fade);
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

  reg.view<Animator, Skin>().each(
    [&](Animator& animator, Skin const& skin) {
      if (!skin.skeleton) {
        return;
      }
      auto const& skeleton = *skin.skeleton;

      UpdateStateMachine(animator, skeleton);

      if (!animator.playing && !animator.resample) {
        return;
      }
      if (!HasMotion(animator, skeleton)) {
        animator.playing = false;
        animator.resample = false;
        return;
      }

      bool finished = false;
      if (HasBlendTree(animator)) {
        StepBlendTree(animator, skeleton, dt);
      } else {
        auto const& clip = skeleton.clips[animator.clip];
        if (animator.playing) {
          finished = StepTime(
            animator.time,
            dt * animator.speed,
            clip.duration,
            animator.loop,
            animator.speed < 0.0f
          );
        }
        SampleClip(clip, animator.time, animator.loop, animator.pose);
      }

      if (animator.playing) {
        animator.fade.time += dt;
      }
      animator.resample = false;

      ApplyFade(animator);
      ApplyPose(reg, skin, animator.pose);

      if (finished) {
        animator.playing = false;
        animator.finished = true;
        LOGD("[Animation] clip \"{}\" finished.", MotionName(animator, skeleton));
      }
    }
  );
}

// ----------------------------------------------------------------------------

void AnimationSystem::postUpdate() {
  LOG_CHECK(scene_ != nullptr);
  auto& reg = scene_->registry;

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

      for (size_t j = 0; j < count; ++j) {
        auto const joint = skin.joints[j];
        auto const* joint_global = (joint != entt::null && reg.valid(joint))
                                 ? reg.try_get<component::GlobalTransform>(joint)
                                 : nullptr;

        skin.matrices[j] = joint_global
          ? lina::mul(lina::mul(inverse_mesh, joint_global->worldMatrix), inverse_binds[j])
          : mat4{lina::identity};
      }
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

    StartClip(animator, *skin.skeleton, clip, loop, fade_duration);

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
  ForEachAnimator(reg, instance_root, [](Animator& animator, Skin& skin) {
    if (!skin.skeleton || !HasMotion(animator, *skin.skeleton)) {
      return;
    }
    if (!HasBlendTree(animator) && !animator.loop) {
      float const duration = skin.skeleton->clips[animator.clip].duration;
      bool const reverse = animator.speed < 0.0f;
      if (reverse ? (animator.time <= 0.0f) : (animator.time >= duration)) {
        animator.time = reverse ? duration : 0.0f;
      }
    }
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
    animator.clip = kInvalidIndexU32;
    animator.time = 0.0f;
    animator.finished = false;
    animator.playing = false;
    animator.resample = false;
    animator.fade.duration = 0.0f;
    animator.blend.tree_index = kInvalidIndexU32;
    animator.blend.phase = 0.0f;
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
  ForEachAnimator(reg, instance_root, [&](Animator& animator, Skin& skin) {
    if (found || !skin.skeleton || !HasMotion(animator, *skin.skeleton)) {
      return;
    }
    found = true;
    result = {
      .clip     = MotionName(animator, *skin.skeleton),
      .time     = animator.time,
      .duration = MotionDuration(animator, *skin.skeleton),
      .speed    = animator.speed,
      .fade     = 1.0f,
      .loop     = HasBlendTree(animator) || animator.loop,
      .playing  = animator.playing,
    };
    if (animator.graph && animator.state.valid()) {
      result.machine_state = animator.graph->states[animator.state.index].name;
    }
    if (auto const& fade = animator.fade; fade.duration > 0.0f) {
      result.fade = std::clamp(fade.time / fade.duration, 0.0f, 1.0f);
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
  ForEachAnimator(reg, instance_root, [time](Animator& animator, Skin& skin) {
    if (!skin.skeleton || !HasMotion(animator, *skin.skeleton)) {
      return;
    }
    if (HasBlendTree(animator)) {
      auto& blend = animator.blend;
      if (blend.duration > lina::kTrueEpsilon) {
        blend.phase = std::clamp(time / blend.duration, 0.0f, 1.0f);
      }
    } else {
      animator.time = std::clamp(time, 0.0f, skin.skeleton->clips[animator.clip].duration);
    }
    animator.fade.duration = 0.0f;
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
    animator.loop = loop;
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

    animator.graph = std::move(compiled);
    animator.parameters = animator.graph->parameter_defaults;
    animator.state = StateId{};
    animator.blend.tree_index = kInvalidIndexU32;   // a tree of the previous graph is gone

    if (animator.graph->initial.valid()) {
      EnterState(animator, *skin.skeleton, animator.graph->initial, 0.0f);
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
    StartBlendTree(animator, tree, fade_duration);
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

float AnimationSystem::parameter(entt::entity instance_root, std::string_view name) const {
  return parameter(instance_root, findParameter(instance_root, name));
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

bool AnimationSystem::setState(entt::entity instance_root, StateId state, float fade_duration) {
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
    EnterState(animator, *skin.skeleton, state, fade_duration);
    ++entered_count;
  });

  if (entered_count == 0u) {
    LOGW("[Animation] invalid state for this instance.");
    return false;
  }
  return true;
}

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
    // Drop the graphs nobody uses anymore, then cache the new one.
    std::erase_if(graph_cache_, [](auto const& entry) { return entry.second.expired(); });
    graph_cache_[key] = graph;
  }
  return graph;
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"
