#ifndef AER_SHADERS_SKINNING_INTEROP_H_
#define AER_SHADERS_SKINNING_INTEROP_H_

static const uint32_t kSkinningGroupSize = 64u;

static const uint32_t kSkinningModeLinear         = 0u;
static const uint32_t kSkinningModeDualQuaternion = 1u;
static const uint32_t kSkinningModeNone           = 3u;

struct SkinVertex {
  uint4  joints;
  float4 weights;
};

struct MorphDelta {
  float4 position;
  float4 normal;
};

struct SkinningPushConstant {
  uint64_t src_vertices;   // Vertex*      bind pose (pool vertex buffer)
  uint64_t skin_vertices;  // SkinVertex*  (pool vertex buffer)
  uint64_t dst_vertices;   // Vertex*      skinned output
  uint64_t palette;        // float4x4 or dual quaternion buffer
  uint64_t morph_deltas;   // MorphDelta*  target-major, morph_count * vertex_count
  uint64_t morph_weights;  // float*       morph_count weights

  uint32_t vertex_count;
  uint32_t joint_count;
  uint32_t mode;
  uint32_t morph_count;
};

#endif // AER_SHADERS_SKINNING_INTEROP_H_