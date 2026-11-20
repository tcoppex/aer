#ifndef AER_SCENE_SKIN_VERTEX_H_
#define AER_SCENE_SKIN_VERTEX_H_

#include <cstddef>
#include <cstdint>

#include "aer/scene/geometry.h"

/* -------------------------------------------------------------------------- */

namespace scene {

//
// Skinning data of a vertex (4 influences max, from JOINTS_0 / WEIGHTS_0).
//
struct SkinVertex {
  static constexpr uint32_t kMaxInfluences{4u};

  uint32_t joints[kMaxInfluences]{};   // indices in Skeleton joints
  float    weights[kMaxInfluences]{};  // normalized (sum to one)

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

static_assert(sizeof(SkinVertex) == 32u, "SkinVertex must stay tightly packed.");

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_SKIN_VERTEX_H_
