#include "aer/scene/ecs/animation_system.h"

#include <algorithm>
#include <cmath>
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
      if (!animator.playing || !skin.skeleton) {
        return;
      }

      auto const& clips = skin.skeleton->clips;
      if (animator.clip >= clips.size()) {
        animator.playing = false;
        return;
      }
      auto const& clip = clips[animator.clip];

      animator.time += dt * animator.speed;

      bool finished = false;
      if (animator.loop) {
        // Keep the time bounded (float precision).
        if (clip.duration > lina::kTrueEpsilon) {
          animator.time = std::fmod(animator.time, clip.duration);
          if (animator.time < 0.0f) {
            animator.time += clip.duration;
          }
        } else {
          animator.time = 0.0f;
        }
      } else if (animator.time >= clip.duration) {
        animator.time = clip.duration;
        finished = true;
      } else if (animator.time <= 0.0f && animator.speed < 0.0f) {
        animator.time = 0.0f;
        finished = true;
      }

      SampleClip(clip, animator.time, animator.loop, animator.pose);
      ApplyPose(reg, skin, animator.pose);

      // The last pose of a non looping clip is still applied above.
      if (finished) {
        animator.playing = false;
        LOGD("[Animation] clip \"{}\" finished.", clip.name);
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
  bool loop
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
    animator.clip = clip;
    animator.time = 0.0f;
    animator.loop = loop;
    animator.playing = true;
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
    if (!skin.skeleton || animator.clip >= skin.skeleton->clips.size()) {
      return;
    }
    if (!animator.loop && animator.time >= skin.skeleton->clips[animator.clip].duration) {
      animator.time = 0.0f;
    }
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
    animator.playing = false;
    animator.clip = kInvalidIndexU32;
    animator.time = 0.0f;

    // (rest_pose is empty for skeletons without animations as they never moved.)
    if (skin.skeleton && !skin.skeleton->rest_pose.joints.empty()) {
      animator.pose = skin.skeleton->rest_pose;
      ApplyPose(reg, skin, animator.pose);
    }
  });
  LOGD("[Animation] stopped.");
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"
