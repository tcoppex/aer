#ifndef AER_SCENE_ANIMATION_H_
#define AER_SCENE_ANIMATION_H_

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aer/core/common.h"
#include "aer/scene/ecs/components.h" // for scene::component::Transform

/* -------------------------------------------------------------------------- */

namespace scene {

struct ParamTag;
using ParamId = Handle<ParamTag>;

struct StateTag;
using StateId = Handle<StateTag>;

struct IKTag;
using IKId = Handle<IKTag>;

struct ChainTag;
using ChainIKId = Handle<ChainTag>;

struct LookTag;
using LookId = Handle<LookTag>;

// struct PackedTransform {
//   vec3 translation{};
//   float scale{1.0f};
//   quat rotation{lina::identity};
// };

enum class SkinningMode : uint32_t {
  Linear         = 0u,
  DualQuaternion = 1u,
};

template<typename T>
using JointBuffer = std::vector<T>;

struct Pose {
  JointBuffer<component::Transform> joints{};
};

struct AnimationClip {
  std::string name{};
  float duration{};   // seconds
  float framerate{};  // samples per second
  std::vector<Pose> poses{};

  void setup(
    std::string_view clip_name,
    uint32_t sampleCount,
    float clip_duration,
    uint32_t jointCount
  ) {
    name = std::string(clip_name);
    duration = clip_duration;
    framerate = (sampleCount > 1u)
              ? static_cast<float>(sampleCount - 1u) / lina::max(duration, lina::kTrueEpsilon)
              : 0.0f;

    poses.resize(sampleCount);
    for (auto& pose : poses) {
      pose.joints.resize(jointCount);
    }
  }
};

struct Skeleton {
  std::string               name{};
  JointBuffer<std::string>  names{};
  JointBuffer<uint32_t>     parents{};
  JointBuffer<uint32_t>     joint_nodes{};      // glTF node index (see PrefabNode::source_node)
  JointBuffer<mat4>         inverse_bind_matrices{};
  JointBuffer<mat4>         global_bind_matrices{};

  Pose                      rest_pose{};        // local joint transforms of the bind pose (empty without animations)

  std::unordered_map<std::string, uint32_t> index_map{};
  std::vector<AnimationClip> clips{}; //


  Skeleton() = default;
  Skeleton(size_t jointCount) { resize(jointCount); }

  [[nodiscard]]
  size_t jointCount() const {
    return global_bind_matrices.size();
  }

  void resize(size_t jointCount) {
    names.assign(jointCount, std::string{});
    parents.assign(jointCount, kInvalidIndexU32);
    joint_nodes.assign(jointCount, kInvalidIndexU32);
    inverse_bind_matrices.assign(jointCount, mat4{lina::identity});
    global_bind_matrices.assign(jointCount, mat4{lina::identity});
    index_map.clear();
  }

  void transformInverseBindMatrices(mat4 const& inv_world) {
    for (auto &inverse_bind_matrix : inverse_bind_matrices) {
      inverse_bind_matrix = lina::mul(inverse_bind_matrix, inv_world);
    }
  }

  [[nodiscard]]
  uint32_t findJoint(std::string const& joint_name) const {
    auto const it = index_map.find(joint_name);
    return (it != index_map.end()) ? it->second : kInvalidIndexU32;
  }

  [[nodiscard]]
  uint32_t findClip(std::string_view clip_name) const {
    for (size_t i = 0; i < clips.size(); ++i) {
      if (clips[i].name == clip_name) {
        return static_cast<uint32_t>(i);
      }
    }
    return kInvalidIndexU32;
  }
};

struct BlendTree1D {
  struct Entry {
    std::string clip{};
    float threshold{};
    uint32_t clip_index{kInvalidIndexU32};
  };

  struct Weights {
    uint32_t a{};
    uint32_t b{};
    float t{};
  };

  std::string name{};
  std::string parameter{};
  std::vector<Entry> entries{};
  ParamId parameter_id{};                 // resolved by CompileAnimationGraph
};

struct AnimationStateMachine {
  struct State {
    std::string name{};
    std::string motion{};                 // clip or blend tree name
    bool loop{true};                      // clip only (tree always loop)
  };

  struct Condition {
    enum class Op : uint8_t {
      Greater,
      Less,
      Trigger
    };
    std::string parameter{};
    Op op{Op::Greater};
    float value{};
  };

  struct Transition {
    std::string from{};
    std::string to{};
    std::vector<Condition> conditions{};
    bool on_finish{false};                // when true, source must be a non looping clip ending
    float exit_time{-1.0f};               // when >= 0.0f, reach time before transition
    float fade{0.25f};                    // cross-fade duration (s)
  };

  std::string name{};
  std::string initial{};
  std::vector<State> states{};
  std::vector<Transition> transitions{};

  [[nodiscard]]
  uint32_t findState(std::string const& state_name) const noexcept {
    for (size_t i = 0; i < states.size(); ++i) {
      if (states[i].name == state_name) {
        return static_cast<uint32_t>(i);
      }
    }
    return kInvalidIndexU32;
  }
};

struct AnimationGraphDesc {
  struct Parameter {
    std::string name{};
    float default_value{};
    bool trigger{false};
    float expiry{0.0f};    // seconds before a pending trigger lapses (0 = never)
  };

  std::string name{};
  std::vector<Parameter> parameters{};      // optionnal
  std::vector<BlendTree1D> trees{};
  AnimationStateMachine machine{};          // optionnal
};

/* Compiled graph resolved against one skeleton.
 * Immutable and shared by every animator using it.*/
struct AnimationGraph {
  struct State {
    std::string name{};
    uint32_t clip{kInvalidIndexU32};
    uint32_t tree{kInvalidIndexU32};        // only set when the motion is a blend tree
    bool loop{true};
  };

  struct Condition {
    ParamId parameter{};
    AnimationStateMachine::Condition::Op op{};
    float value{};
  };

  struct Transition {
    StateId from{};
    StateId to{};
    std::vector<Condition> conditions{};
    bool on_finish{false};
    float exit_time{-1.0f};
    float fade{0.25f};
  };

  std::string name{};

  std::shared_ptr<AnimationGraphDesc const> source{};
  std::vector<AnimationGraphDesc::Parameter> parameters{};

  std::vector<BlendTree1D>  trees{};
  std::vector<State>        states{};
  std::vector<Transition>   transitions{};
  StateId                   initial{};      // invalid when there is no machine

  [[nodiscard]]
  ParamId findParameter(std::string_view n) const noexcept {
    for (size_t i = 0; i < parameters.size(); ++i) {
      if (parameters[i].name == n) {
        return ParamId{static_cast<uint32_t>(i)};
      }
    }
    return {};
  }

  [[nodiscard]]
  StateId findState(std::string_view n) const noexcept {
    for (size_t i = 0; i < states.size(); ++i) {
      if (states[i].name == n) {
        return StateId{static_cast<uint32_t>(i)};
      }
    }
    return {};
  }

  [[nodiscard]]
  uint32_t findTree(std::string_view n) const noexcept {
    for (size_t i = 0; i < trees.size(); ++i) {
      if (trees[i].name == n) {
        return static_cast<uint32_t>(i);
      }
    }
    return kInvalidIndexU32;
  }
};

/* -------------------------------------------------------------------------- */

[[nodiscard]]
component::Transform BlendTransforms(
  component::Transform const& a,
  component::Transform const& b,
  float t
);

void BlendPoses(Pose const& a, Pose const& b, float t, Pose& out);

void SampleClip(AnimationClip const& clip, float time, bool loop, Pose& out);

BlendTree1D::Weights ComputeBlendTreeWeights(BlendTree1D const& tree, float value);

/* Resolve a graph description against a skeleton (clip indices, parameter and state ids).
 * Returns null when a name can't be resolved. */
[[nodiscard]]
std::shared_ptr<AnimationGraph const> CompileAnimationGraph(
  std::shared_ptr<AnimationGraphDesc const> desc,
  Skeleton const& skeleton
);

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_H_
