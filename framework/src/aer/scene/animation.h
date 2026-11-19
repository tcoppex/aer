#ifndef AER_SCENE_ANIMATION_H_
#define AER_SCENE_ANIMATION_H_

#include <unordered_map>
#include <vector>

#include "aer/core/common.h"
#include "aer/scene/ecs/components.h" // for scene::components::transform

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
  JointBuffer<std::string>  names{};
  JointBuffer<uint32_t>     parents{kInvalidIndexU32};
  JointBuffer<mat4>         inverse_bind_matrices{};
  JointBuffer<mat4>         global_bind_matrices{};

  std::unordered_map<std::string, uint32_t> index_map{};
  std::vector<AnimationClip*> clips{};

  Skeleton() = default;
  Skeleton(size_t jointCount) { resize(jointCount); }

  [[nodiscard]]
  size_t jointCount() const {
    return global_bind_matrices.size();
  }

  void resize(size_t jointCount) {
    names.resize(jointCount);
    parents.resize(jointCount);
    inverse_bind_matrices.resize(jointCount);
    global_bind_matrices.resize(jointCount);
    index_map.clear();
  }

  void transformInverseBindMatrices(mat4 const& inv_world) {
    for (auto &inverse_bind_matrix : inverse_bind_matrices) {
      inverse_bind_matrix = lina::mul(inverse_bind_matrix, inv_world);
    }
  }
};

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_H_
