#include "aer/scene/animation.h"

#include <cmath>
#include <algorithm>
#include <memory>

/* -------------------------------------------------------------------------- */

namespace scene {

component::Transform BlendTransforms(
  component::Transform const& a,
  component::Transform const& b,
  float t
) {
  component::Transform out{};
  out.translation = lina::lerp(a.translation, b.translation, t);
  out.rotation    = lina::qlerp(a.rotation, b.rotation, t);
  out.scale       = lina::lerp(a.scale, b.scale, t);
  return out;
}

// ----------------------------------------------------------------------------

void BlendPoses(Pose const& a, Pose const& b, float t, Pose& out) {
  if (a.joints.size() != b.joints.size()) {
    LOGE("BlendPoses: joint count mismatch ({} vs {}).", a.joints.size(), b.joints.size());
    return;
  }
  out.joints.resize(a.joints.size());

  for (size_t i = 0; i < a.joints.size(); ++i) {
    out.joints[i] = BlendTransforms(a.joints[i], b.joints[i], t);
  }
}

// ----------------------------------------------------------------------------

void SampleClip(AnimationClip const& clip, float time, bool loop, Pose& out) {
  size_t const sample_count = clip.poses.size();
  if (sample_count == 0u) {
    return;
  }

  // Static pose.
  if (sample_count == 1u || clip.duration <= lina::kTrueEpsilon) {
    out = clip.poses.front();
    return;
  }

  if (loop) {
    time = std::fmod(time, clip.duration);
    if (time < 0.0f) {
      time += clip.duration;
    }
  } else {
    time = std::clamp(time, 0.0f, clip.duration);
  }

  float const position = time * clip.framerate;
  size_t const i0 = std::min(static_cast<size_t>(position), sample_count - 2u);
  float const alpha = std::clamp(position - static_cast<float>(i0), 0.0f, 1.0f);

  BlendPoses(clip.poses[i0], clip.poses[i0 + 1u], alpha, out);
}

// ----------------------------------------------------------------------------

BlendTree1D::Weights ComputeBlendTreeWeights(BlendTree1D const& tree, float value) {
  auto const& e = tree.entries;
  if (value <= e.front().threshold) {
    return {0u, 0u, 0.0f};
  }
  if (value >= e.back().threshold) {
    auto s = static_cast<uint32_t>(e.size());
    return {s - 1u, s -1u, 0.0f};
  }
  uint32_t i = 1u;
  while (e[i].threshold < value) {
    ++i;
  }
  float const span = e[i].threshold - e[i - 1u].threshold;
  float const t = (span > lina::kTrueEpsilon)
    ? (value - e[i - 1u].threshold) / span
    : 0.0f;
  return {i - 1u, i, t};
}

// ----------------------------------------------------------------------------

std::shared_ptr<AnimationGraph const> CompileAnimationGraph(
  std::shared_ptr<AnimationGraphDesc const> desc,
  Skeleton const& skeleton
) {
  if (!desc) {
    return nullptr;
  }
  auto const& d = *desc;

  auto graph = std::make_shared<AnimationGraph>();
  graph->name = d.name;
  graph->source = desc;

 // false when a parameter is used both as a trigger and as a value
  bool valid = true;

  /* Declare a parameter once, an empty name means "no parameter" (always 0). */
  auto const declare = [&](
    std::string const& name,
    bool trigger,
    float default_value = 0.0f,
    float expiry = 0.0f
  ) -> ParamId {
    if (name.empty()) {
      return {};
    }
    if (auto const id = graph->findParameter(name); id.valid()) {
      if (graph->parameters[id.index].trigger != trigger) {
        LOGE("[Animation] graph \"{}\": parameter \"{}\" is used both as a trigger and as a value.",
             d.name, name);
        valid = false;
      }
      return id;
    }
    graph->parameters.push_back(AnimationGraphDesc::Parameter{
      .name          = name,
      .default_value = trigger ? 0.0f : default_value,
      .trigger       = trigger,
      .expiry        = expiry,
    });
    return ParamId{static_cast<uint32_t>(graph->parameters.size() - 1u)};
  };

  for (auto const& p : d.parameters) {
    declare(p.name, p.trigger, p.default_value, p.expiry);
  }

  // Blend trees.
  for (auto const& src : d.trees) {
    if (src.entries.empty()) {
      LOGE("[Animation] graph \"{}\": blend tree \"{}\" has no entry.", d.name, src.name);
      return nullptr;
    }
    if (graph->findTree(src.name) != kInvalidIndexU32) {
      LOGE("[Animation] graph \"{}\": duplicate blend tree \"{}\".", d.name, src.name);
      return nullptr;
    }

    BlendTree1D tree = src;
    std::ranges::stable_sort(tree.entries, {}, &BlendTree1D::Entry::threshold);
    for (auto& entry : tree.entries) {
      entry.clip_index = skeleton.findClip(entry.clip);
      if (entry.clip_index == kInvalidIndexU32) {
        LOGW("[Animation] graph \"{}\": blend tree \"{}\": no clip \"{}\" in skeleton \"{}\".",
             d.name, tree.name, entry.clip, skeleton.name);
        return nullptr;
      }
    }
    tree.parameter_id = declare(tree.parameter, false);
    graph->trees.push_back(std::move(tree));
  }

  // State machine.
  auto const& machine = d.machine;
  for (auto const& src : machine.states) {
    if (graph->findState(src.name).valid()) {
      LOGE("[Animation] graph \"{}\": duplicate state \"{}\".", d.name, src.name);
      return nullptr;
    }

    AnimationGraph::State state{ .name = src.name, .loop = src.loop };
    state.clip = skeleton.findClip(src.motion);
    if (state.clip == kInvalidIndexU32) {
      state.tree = graph->findTree(src.motion);
      if (state.tree == kInvalidIndexU32) {
        LOGW("[Animation] graph \"{}\": no clip or blend tree \"{}\".", d.name, src.motion);
        return nullptr;
      }
    }
    graph->states.push_back(std::move(state));
  }

  for (auto const& src : machine.transitions) {
    bool const has_origin = !src.from.empty();

    AnimationGraph::Transition transition{
      .on_finish = src.on_finish,
      .exit_time = src.exit_time,
      .fade      = src.fade,
    };
    transition.to = graph->findState(src.to);
    if (has_origin) {
      transition.from = graph->findState(src.from);
    }
    if (!transition.to.valid() || (has_origin && !transition.from.valid())) {
      LOGW("[Animation] graph \"{}\": unknown state in transition \"{}\" -> \"{}\".",
           d.name, src.from, src.to);
      return nullptr;
    }

    for (auto const& c : src.conditions) {
      bool const trigger = (c.op == AnimationStateMachine::Condition::Op::Trigger);
      transition.conditions.push_back(
        AnimationGraph::Condition{
          .parameter = declare(c.parameter, trigger),
          .op = c.op,
          .value = c.value
        }
      );
    }
    graph->transitions.push_back(std::move(transition));
  }
  if (!valid) {
    return nullptr;
  }

  if (!graph->states.empty()) {
    graph->initial = machine.initial.empty()
                    ? StateId{0u}
                    : graph->findState(machine.initial);
    if (!graph->initial.valid()) {
      LOGW("[Animation] graph \"{}\": unknown initial state \"{}\".",
        d.name, machine.initial);
      return nullptr;
    }
  }

  LOGD("[Animation] graph \"{}\" compiled for \"{}\" ({} params, {} trees, {} states).",
    d.name,
    skeleton.name,
    graph->parameters.size(),
    graph->trees.size(),
    graph->states.size()
  );
  return graph;
}

}  // namespace "scene"

/* -------------------------------------------------------------------------- */
