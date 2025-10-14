#ifndef AER_RENDERER_RAYTRACING_SCENE_H_
#define AER_RENDERER_RAYTRACING_SCENE_H_

#include "aer/platform/vulkan/context.h"
#include "aer/platform/vulkan/accel_struct.h"
#include "aer/scene/host_resources.h" // for scene::ResourceBuffer
#include "aer/scene/mesh.h"

/* -------------------------------------------------------------------------- */

class RayTracingSceneInterface {
 public:
  virtual ~RayTracingSceneInterface() = default;

  virtual void init(Context const& ctx) = 0;

  /* Release internal buffers. */
  virtual void release() = 0;

  /* Build the Acceleration structures & Instances data buffer. */
  virtual void build(
    scene::ResourceBuffer<scene::Mesh> const& meshes,
    backend::Buffer const& vertex_buffer,
    backend::Buffer const& index_buffer
  ) = 0;

  /* Return the Top Level Acceleration Structure. */
  [[nodiscard]]
  virtual backend::TLAS const& tlas() const = 0;

  /* Return the Instances data buffer. */
  [[nodiscard]]
  virtual backend::Buffer instances_data_buffer() const = 0;

  // TODO:
  // add tlas update
  // Add tlas rebuild

 protected:
  /* Build the Bottom Level Acceleration Structure. */
  virtual bool build_blas(scene::Mesh::SubMesh const& submesh) = 0;

  /* Build the Top Level Acceleration Structure. */
  virtual void build_tlas() = 0;

  /* Build the Instances data buffer. */
  virtual void build_instances_data_buffer(
    scene::ResourceBuffer<scene::Mesh> const& meshes,
    backend::Buffer const& vertex_buffer,
    backend::Buffer const& index_buffer
  ) = 0;
};

// ----------------------------------------------------------------------------

///
/// Acceleration Structure for a basic raytracer.
///
class RayTracingScene : public RayTracingSceneInterface {
 public:
  struct InstanceData {
    VkDeviceAddress vertex{};
    VkDeviceAddress index{};
  };

 public:
  RayTracingScene() = default;

  virtual ~RayTracingScene() {
    release();
  }

  void init(Context const& ctx) final;

  void release() final;

  void build(
    scene::ResourceBuffer<scene::Mesh> const& meshes,
    backend::Buffer const& vertex_buffer,
    backend::Buffer const& index_buffer
  ) final;


  backend::TLAS const& tlas() const override {
    return tlas_;
  }

  backend::Buffer instances_data_buffer() const override {
    return instances_data_buffer_;
  }

 protected:
  bool build_blas(scene::Mesh::SubMesh const& submesh) final;

  void build_tlas() final;

  void build_instances_data_buffer(
    scene::ResourceBuffer<scene::Mesh> const& meshes,
    backend::Buffer const& vertex_buffer,
    backend::Buffer const& index_buffer
  ) override;

 private:
  void build_acceleration_structure(
    backend::AccelerationStructure* as,
    VkPipelineStageFlags2 dstStageMask,
    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo
  );

 private:
  Context const* context_ptr_{};
  VkDeviceAddress vertex_address_{};
  VkDeviceAddress index_address_{};

  std::vector<backend::BLAS> blas_{}; // one per submesh
  backend::TLAS tlas_{};

  backend::Buffer scratch_buffer_{};
  backend::Buffer instances_data_buffer_{};
};

/* -------------------------------------------------------------------------- */

#endif