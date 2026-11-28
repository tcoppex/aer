#ifndef AER_SCENE_SKIN_VERTEX_H_
#define AER_SCENE_SKIN_VERTEX_H_

#include <cstddef>
#include <cstdint>

#include "aer/scene/geometry.h"

namespace skinning_shader_interop {
#include "aer/shaders/skinning/interop.h"
}

/* -------------------------------------------------------------------------- */

namespace scene {

//
// Skinning data of a vertex (4 influences max, from JOINTS_0 / WEIGHTS_0).
// [move to renderer/ along vertex_layout.h ?]
//
struct SkinVertex : skinning_shader_interop::SkinVertex {
  static constexpr uint32_t kMaxInfluences{4u};

  // uint32_t joints[kMaxInfluences]{};   // indices in Skeleton joints
  // float    weights[kMaxInfluences]{};  // normalized (sum to one)

  [[nodiscard]]
  static
  Geometry::AttributeInfoMap GetAttributeInfoMap() {
    return {
      {
        Geometry::AttributeType::Joints,
        {
          .format = Geometry::AttributeFormat::RGBA_U32,
          .offset = offsetof(SkinVertex, joints),
          .stride = sizeof(SkinVertex),
        }
      },
      {
        Geometry::AttributeType::Weights,
        {
          .format = Geometry::AttributeFormat::RGBA_F32,
          .offset = offsetof(SkinVertex, weights),
          .stride = sizeof(SkinVertex),
        }
      },
    };
  }
};

using MorphDelta = skinning_shader_interop::MorphDelta;

static_assert(sizeof(SkinVertex) == 32u, "SkinVertex must stay tightly packed.");
static_assert(sizeof(MorphDelta) == 32u, "MorphDelta must stay tightly packed.");

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_SKIN_VERTEX_H_
