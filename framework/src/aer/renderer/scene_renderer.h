#ifndef AER_RENDERER_SCENE_RENDERER_H_
#define AER_RENDERER_SCENE_RENDERER_H_

#include <map>
#include <utility>
#include <vector>

#include "aer/core/common.h"
#include "aer/core/enum_array.h"
#include "aer/platform/vulkan/types.h"
#include "aer/renderer/gpu_pool.h"
#include "aer/renderer/fx/material/material_fx_registry.h"
#include "aer/scene/ecs/hierarchy.h"

class Camera;
class RenderContext;
class RenderPassEncoder;

enum class RendererFlags : uint32_t;

/* -------------------------------------------------------------------------- */

///
/// Per-frame drawing
/// owns what depends on scene content (draw lists, transforms, TLAS)
/// allocated as ring buffers
///
class SceneRenderer {
 public:
  bool setup(RenderContext const&, GpuPool&, MaterialFxRegistry&, uint32_t frames_in_flight);

  void release();

  void update(
    scene::Hierarchy&,
    Camera const&,
    float elapsed_time,
    uint32_t frame_index,
    RendererFlags renderer_flags
  );

  void render(RenderPassEncoder const&);

  // void onFramesInFlightChanged(uint32_t frames_in_fligh);

 private:
  bool growTransforms(uint32_t min_count);

  void uploadTransforms();

  void collectRetired();

  void buildDrawLists(scene::Hierarchy&, Camera const&);

  void updateFrameData(Camera const& camera, float elapsed_time, RendererFlags renderer_flags);

 private:
  static constexpr uint32_t kInitialRenderableCapacity = 1024u;
  static constexpr VkDeviceSize kSliceAlignment = 256u;

 private:
  struct RetiredBuffer {
    backend::Buffer buffer{};
    uint64_t frame{};
  };

  struct DrawItem {
    DeviceSubmesh const*  submesh{};
    DeviceMaterial const* material{};
    DeviceMesh const*     mesh{};
    uint32_t              transform_slot{};
    float                 sort_key{};
  };

  using FxHashPair        = std::pair<MaterialFx*, scene::MaterialStates>;
  using DrawItems         = std::vector<DrawItem>;
  using FxDrawablesLookup = std::map<FxHashPair, DrawItems>;
  using AlphaMode         = scene::MaterialStates::AlphaMode;

 private:
  RenderContext const* context_ptr_{};
  GpuPool* pool_ptr_{};

  uint32_t ring_slot_{}; //

  uint32_t frames_in_flight_{};
  uint64_t frame_counter_{}; //

  backend::Buffer frame_sbo_{};
  VkDeviceSize    frame_data_stride_{};
  VkDeviceAddress frame_data_current_address_{};

  backend::Buffer   transforms_sbo_{};
  uint32_t          transforms_capacity_{};
  VkDeviceSize      transforms_slice_stride_{};
  VkDeviceAddress   transforms_current_address_{};
  std::vector<mat4> transforms_staging_{};

  std::vector<RetiredBuffer> retired_buffers_{};

  // [TODO: for the TLAS part]
  // RayTracingFx* ray_tracing_fx_{}; //

  EnumArray<FxDrawablesLookup, AlphaMode> lookups_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_SCENE_RENDERER_H_