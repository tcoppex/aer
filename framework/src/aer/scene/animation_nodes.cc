#include "aer/scene/animation_nodes.h"

#include <algorithm>
#include <cmath>

/* -------------------------------------------------------------------------- */

namespace scene {

namespace {

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

}  // namespace

/* -------------------------------------------------------------------------- */

void ClipNode::evaluate(PoseContext const& ctx, Pose& out) {
  reverse_ = ctx.reverse;
  if (ctx.playing) {
    finished_ = StepTime(time_, ctx.delta, clip_->duration, loop_, ctx.reverse)
             || finished_;
  }
  SampleClip(*clip_, time_, loop_, out);
}

// ----------------------------------------------------------------------------

float ClipNode::normalizedTime() const {
  if (clip_->duration <= lina::kTrueEpsilon) {
    return 1.0f;
  }
  float const t = std::clamp(time_ / clip_->duration, 0.0f, 1.0f);
  return reverse_ ? (1.0f - t) : t;
}

// ----------------------------------------------------------------------------

void ClipNode::seek(float time) {
  time_ = std::clamp(time, 0.0f, clip_->duration);
}

// ----------------------------------------------------------------------------

void ClipNode::onResume(bool reverse) {
  if (!loop_) {
    float const duration = clip_->duration;
    if (reverse ? (time_ <= 0.0f) : (time_ >= duration)) {
      time_ = reverse ? duration : 0.0f;
    }
  }
  finished_ = false;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void BlendNode1D::evaluate(PoseContext const& ctx, Pose& out) {
  auto const id = tree_->parameter_id;
  float const value = (id.valid() && id.index < ctx.parameters.size())
                    ? ctx.parameters[id.index]
                    : 0.0f;

  auto const w = ComputeBlendTreeWeights(*tree_, value);

  auto const& clip_a = ctx.skeleton.clips[tree_->entries[w.a].clip_index];
  auto const& clip_b = ctx.skeleton.clips[tree_->entries[w.b].clip_index];

  float const duration = std::max(
    clip_a.duration + (clip_b.duration - clip_a.duration) * w.t,
    lina::kTrueEpsilon
  );

  if (ctx.playing) {
    phase_ += ctx.delta / duration;
    phase_ -= std::floor(phase_);   // wrap into [0, 1)
    if (phase_ >= 1.0f) {
      phase_ = 0.0f;
    }
  }

  SampleClip(clip_a, phase_ * clip_a.duration, true, out);
  if ((w.b != w.a) && (w.t > 0.0f)) {
    SampleClip(clip_b, phase_ * clip_b.duration, true, scratch_);
    BlendPoses(out, scratch_, w.t, out);
  }

  duration_ = duration;
}

// ----------------------------------------------------------------------------

float BlendNode1D::normalizedTime() const {
  return reverse_ ? (1.0f - phase_) : phase_;
}

// ----------------------------------------------------------------------------

void BlendNode1D::seek(float time) {
  if (duration_ > lina::kTrueEpsilon) {
    phase_ = std::clamp(time / duration_, 0.0f, 1.0f);
  }
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void FadeNode::evaluate(PoseContext const& ctx, Pose& out) {
  child_->evaluate(ctx, out);

  if (ctx.playing) {
    time_ += ctx.dt;
  }
  if (done()) {
    from_ = {};
    return;
  }

  float const alpha = std::clamp(time_ / duration_, 0.0f, 1.0f);
  float const smooth = alpha * alpha * (3.0f - 2.0f * alpha); // smoothstep
  BlendPoses(from_, out, smooth, out);
}

// ----------------------------------------------------------------------------

float FadeNode::fadeProgress() const {
  return (duration_ > lina::kTrueEpsilon)
        ? std::clamp(time_ / duration_, 0.0f, 1.0f)
        : 1.0f;
}

// ----------------------------------------------------------------------------

void FadeNode::seek(float time) {
  child_->seek(time);
  time_ = duration_;
  from_ = {};
}

// ----------------------------------------------------------------------------

std::unique_ptr<PoseNode> FadeNode::takeChildIfDone() {
  if (!done()) {
    return nullptr;
  }
  return std::move(child_);
}

}  // namespace "scene"

/* -------------------------------------------------------------------------- */