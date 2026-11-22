#include "aer/scene/animation.h"

#include <algorithm>
#include <cmath>

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

  #pragma omp parallel for schedule(static) num_threads(4)
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

}  // namespace "scene"

/* -------------------------------------------------------------------------- */
