#ifndef AER_RENDERER_SCENE_RENDERER_H_
#define AER_RENDERER_SCENE_RENDERER_H_

#include <map>
#include <utility>
#include <unordered_map>
#include <vector>

#include "aer/core/common.h"
#include "aer/core/enum_array.h"
#include "aer/platform/vulkan/types.h"
#include "aer/scene/ecs/hierarchy.h"
#include "aer/renderer/gpu_pool.h"
#include "aer/renderer/blas_builder.h"
#include "aer/renderer/vertex_layout.h" //
#include "aer/renderer/ray_tracing_view.h"
#include "aer/renderer/fx/material/material_fx_registry.h"

class Camera;
class RenderContext;
class CommandEncoder;
class RenderPassEncoder;

enum class RendererFlags : uint32_t;

/* -------------------------------------------------------------------------- */

///
/// Per-frame drawing
/// owns what depends on scene content (draw lists, transforms, Skinning, TLAS)
/// allocated as ring buffers.
///
/// (TODO? move specifics to custom classes, like Skinning & RayTracing)
///
class SceneRenderer {
 private:
  static constexpr uint32_t kInitialRenderableCapacity = 1024u;
  static constexpr VkDeviceSize kSliceAlignment = 256u;

  static constexpr uint32_t kJointsWarnThreshold = 256u;
  static constexpr VkDeviceSize kSkinnedAlignment = 16u;

 private:
  struct RetiredBuffer {
    backend::Buffer buffer{};
    uint64_t frame{};
  };

  struct SliceBuffer {
    backend::Buffer buffer{};
    VkDeviceSize    stride{};
    VkDeviceAddress current_address{};
  };

  struct SkinningJob {
    uint64_t vertex_offset{};
    uint64_t skin_offset{kInvalidIndexU64};
    uint64_t morph_offset{kInvalidIndexU64};
    uint64_t out_offset{};

    uint32_t palette_offset{};    // in mat4 slots of palette_staging_
    uint32_t weights_offset{};    // same
    uint32_t joint_count{};
    uint32_t vertex_count{};
    uint32_t morph_count{};
    uint32_t mode{};
  };

  struct DrawItem {
    DeviceSubmesh const*  submesh{};
    DeviceMaterial const* material{};
    DeviceMesh const*     mesh{};
    uint32_t              transform_slot{};
    uint64_t              skinned_offset{kInvalidIndexU64};
    float                 sort_key{};
  };

  using FxHashPair        = std::pair<MaterialFx*, scene::MaterialStates>;
  using DrawItems         = std::vector<DrawItem>;
  using FxDrawablesLookup = std::map<FxHashPair, DrawItems>;
  using AlphaMode         = scene::MaterialStates::AlphaMode;

 public:
  SceneRenderer();
  ~SceneRenderer();

  bool setup(RenderContext const&, GpuPool&, MaterialFxRegistry&, uint32_t frames_in_flight);

  void release();

  void update(
    scene::Hierarchy&,
    Camera const&,
    float elapsed_time,
    RendererFlags renderer_flags
  );

  /* Display transform applied at the end of the lit materials :
   * linear exposure scale (2^EV) and tonemapping operator (Tonemapping value). */
  void set_display(float exposure, uint32_t tonemapping) noexcept {
    exposure_ = exposure;
    tonemapping_ = tonemapping;
  }

  void upload(uint32_t frame_index);

  void computeSkinning(CommandEncoder const&);

  void render(RenderPassEncoder const&);

  // void onFramesInFlightChanged(uint32_t frames_in_fligh);

 private:
  bool growTransforms(uint32_t min_count);

  void uploadTransforms();

  void collectRetired();

  // ---

  bool ensureSliceBuffer(
    SliceBuffer&,
    VkDeviceSize min_slice_bytes,
    VkBufferUsageFlags2KHR usage,
    VmaMemoryUsage memory_usage,
    VmaAllocationCreateFlags flags
  );

  bool setupSkinningPipeline();

  void buildSkinning(scene::Hierarchy&);

  void uploadSkinning();

  // ---

  void buildDrawLists(scene::Hierarchy&, Camera const&);

  void buildFrameData(Camera const& camera, float elapsed_time, RendererFlags renderer_flags);

  void writeFrameData();

 private:
  RenderContext const* context_ptr_{};
  GpuPool* pool_ptr_{};

  bool pending_upload_{};
  uint32_t ring_slot_{}; //

  uint32_t frames_in_flight_{};
  uint64_t frame_counter_{}; //

  float    exposure_{1.0f};
  uint32_t tonemapping_{};

  backend::Buffer frame_sbo_{};
  VkDeviceSize    frame_data_stride_{};
  VkDeviceAddress frame_data_current_address_{};
  material_shader_interop::FrameData frame_data_{}; //

  std::vector<RetiredBuffer> retired_buffers_{};

  // -- Transforms --
  backend::Buffer   transforms_sbo_{};
  uint32_t          transforms_capacity_{};
  VkDeviceSize      transforms_slice_stride_{};
  VkDeviceAddress   transforms_current_address_{};
  std::vector<mat4> transforms_staging_{};

  // -- Draw Lists --
  EnumArray<FxDrawablesLookup, AlphaMode> lookups_{};

  // -- GPU Skinning (CPU side) --
  std::vector<SkinningJob> skinning_jobs_{};
  std::vector<mat4>        palette_staging_{};
  uint64_t                 skinned_bytes_{};
  SliceBuffer              palette_{};
  SliceBuffer              skinned_{};
  VkPipelineLayout         skinning_layout_{};
  Pipeline                 skinning_pipeline_{};
  bool                     skinning_pending_{};

  std::unordered_map<entt::entity, uint32_t> skinned_first_job_{};

  // -- Ray tracing (CPU side) --
 public:
  /* When false skip building the raster draw lists (eg. for ray traced only frame). */
  void set_rasterization_enabled(bool enabled) noexcept {
    raster_enabled_ = enabled;
  }

  /* When false skip building TLAS data. */
  void set_ray_tracing_enabled(bool enabled);

  void recordAccelerationStructures(CommandEncoder const& cmd);

  // [[nodiscard]]
  // uint64_t ray_tracing_version() const noexcept { return rt_version_; }

  /* Current frame's ray tracing data.
   * Invalid when RT is disabled, nothing is traceable, or recordAccelerationStructures()
   * was not called this frame. */
  [[nodiscard]]
  RayTracingView ray_tracing_view() const;

 private:
  static constexpr uint64_t kNoVersion = kInvalidIndexU64;

  // Rebuild skinned meshes BLAS every N frames to avoid drift.
  static constexpr uint32_t kSkinnedBLASRebuildPeriod = 60u;

  /* BLAS of one dynamic entity, for one frame slot (refit every frame). */
  struct RTDynamicBLAS {
    backend::AccelerationStructure  blas{};
    DeviceMesh const*               mesh{};
    uint32_t                        since_rebuild{};
    bool                            built{};
    bool                            used{};
  };

  /* A dynamic instance of the TLAS (collected at update, patched at record). */
  struct RTDynamicInstance {
    entt::entity                    entity{};
    DeviceAsset const*              asset{};
    DeviceMesh const*               mesh{};
    uint32_t                        instance_index{};
    uint32_t                        first_geometry{};
    uint32_t                        first_job{};
  };

  struct RayTracingFrame {
    struct Dynamic {
      std::unordered_map<entt::entity, RTDynamicBLAS> blas{};
      backend::Buffer scratch{};
      VkDeviceSize scratch_size{};
    };

    backend::AccelerationStructure  tlas{};
    backend::Buffer                 tlas_scratch{};
    backend::Buffer                 instances{};
    backend::Buffer                 geometry_table{};
    Dynamic                         dynamic{};
    uint32_t                        instance_capacity{};
    uint32_t                        geometry_capacity{};
    uint32_t                        instance_count{};
    uint64_t                        built_version{kNoVersion};
  };

  void collectRayTracingInstances(scene::Hierarchy& scene);

  bool ensureRayTracingFrame(RayTracingFrame&);

  void recordDynamicBLAS(CommandEncoder const&, RayTracingFrame&);

  void releaseRayTracing();

  struct {
    bool enabled{};
    uint64_t version{};

    std::vector<VkAccelerationStructureInstanceKHR> instances{};
    std::vector<VkAccelerationStructureInstanceKHR> prev_instances{};
    std::vector<material_shader_interop::RTGeometryData> geometries{};
    std::vector<material_shader_interop::RTGeometryData> prev_geometries{};
    std::vector<RayTracingFrame> frames{};
    std::vector<RTDynamicInstance> dynamic_instances{}; // (mainly skinned mesh currently)
  } rt_;

  bool raster_enabled_{true}; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_SCENE_RENDERER_H_