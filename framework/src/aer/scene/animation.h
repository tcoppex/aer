#ifndef AER_SCENE_ANIMATION_H_
#define AER_SCENE_ANIMATION_H_

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "aer/core/common.h"
#include "aer/scene/ecs/components.h" // for scene::component::Transform

/* -------------------------------------------------------------------------- */

namespace scene {

// struct PackedTransform {
//   vec3 translation{};
//   float scale{1.0f};
//   quat rotation{lina::identity};
// };

template<typename T>
using JointBuffer = std::vector<T>;

struct Pose {
  JointBuffer<component::Transform> joints{};
};

struct AnimationClip {
  std::string name{};
  float duration{};
  float framerate{};
  std::vector<Pose> poses{};

  void setup(
    std::string_view clip_name,
    uint32_t sampleCount,
    float clip_duration,
    uint32_t jointCount
  ) {
    name = std::string(clip_name);
    duration = clip_duration;
    framerate = static_cast<float>(sampleCount) / lina::max(duration, lina::kTrueEpsilon);

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

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_H_
