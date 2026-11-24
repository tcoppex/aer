#ifndef AER_SCENE_ANIMATION_NODES_H_
#define AER_SCENE_ANIMATION_NODES_H_

#include <span>
#include <string_view>

#include "aer/scene/animation.h"

/* -------------------------------------------------------------------------- */

namespace scene {

struct PoseContext {
  Skeleton const& skeleton;
  std::span<float const> parameters{};  // animator values, indexed by ParamId
  float delta{};                        // signed seconds to advance (dt * speed)
  bool reverse{false};                  // playback direction (speed < 0)
  bool playing{false};
};

//
//    PoseNode
//
//  Runtime node of an animator, owns its playback state.
//
class PoseNode {
 public:
  virtual ~PoseNode() = default;

  virtual void evaluate(PoseContext const& ctx, Pose& out) = 0;

  [[nodiscard]] virtual std::string_view name() const = 0;
  [[nodiscard]] virtual float time() const = 0;            // seconds
  [[nodiscard]] virtual float duration() const = 0;        // seconds
  [[nodiscard]] virtual float normalizedTime() const = 0;  // [0, 1]
  [[nodiscard]] virtual bool finished() const { return false; }
  [[nodiscard]] virtual bool looping() const { return true; }

  virtual void seek(float /*time*/) {}
  virtual void setLoop(bool /*loop*/) {}

  virtual void onResume(bool /*reverse*/) {}
};

// ----------------------------------------------------------------------------

class ClipNode final : public PoseNode {
 public:
  ClipNode(AnimationClip const& clip, bool loop, bool reverse)
    : clip_(&clip)
    , time_(reverse ? clip.duration : 0.0f)
    , loop_(loop)
  {}

  void evaluate(PoseContext const& ctx, Pose& out) override;

  std::string_view name() const override { return clip_->name; }

  float time() const override { return time_; }
  float duration() const override { return clip_->duration; }
  float normalizedTime() const override;
  bool finished() const override { return finished_; }
  bool looping() const override { return loop_; }

  void seek(float time) override;
  void setLoop(bool loop) override { loop_ = loop; }
  void onResume(bool reverse) override;

 private:
  AnimationClip const* clip_{};
  float time_{};
  bool loop_{true};
  bool finished_{false};
};

// ----------------------------------------------------------------------------

class BlendNode1D final : public PoseNode {
 public:
  explicit
  BlendNode1D(BlendTree1D const& tree)
    : tree_(&tree)
  {}

  void evaluate(PoseContext const& ctx, Pose& out) override;

  std::string_view name() const override { return tree_->name; }
  float time() const override { return phase_ * duration_; }
  float duration() const override { return duration_; }
  float normalizedTime() const override { return phase_; }

  void seek(float time) override;

 private:
  BlendTree1D const* tree_{};     // from animator graph (must outlives the node)
  float phase_{};
  float duration_{};
  Pose scratch_{};
};

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_NODES_H_
