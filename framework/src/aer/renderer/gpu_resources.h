#ifndef AER_RENDERER_GPU_RESOURCES_H_
#define AER_RENDERER_GPU_RESOURCES_H_

#include "aer/scene/host_resources.h"

#include "aer/renderer/raytracing_scene.h"
#include "aer/renderer/fx/material/material_fx_registry.h"

class Camera;
class RenderContext;
class RenderPassEncoder;
class RayTracingFx;

/* -------------------------------------------------------------------------- */

/**
 * @class GPUResources
 * 
 * Represent Scene data on GPU to be used for rendering.
 * (As is, using multiple instances of it is not ideal)
 */
struct GPUResources : scene::HostResources {
 public:
  using UploadFlags = uint32_t;

  enum UploadFlagBits : UploadFlags {
    kUploadFlagBits_None                     = 0,
    kUploadFlagBits_ReleaseHostDataOnUpload  = 1 << 0,
    kUploadFlagBits_BuildRayTracingData      = 1 << 1,

    kUploadFlagBits_Default = kUploadFlagBits_ReleaseHostDataOnUpload
  };

 public:
  GPUResources(
    RenderContext const& context,
    uint32_t max_frames_in_flight //
  );

  ~GPUResources();

  /* Load a scene assets from disk to Host memory. */
  bool loadFile(std::string_view filename);

  /* Bind mesh attributes to pipeline locations. */
  void initializeSubmeshDescriptors(
    scene::Mesh::AttributeLocationMap const& attribute_to_location
  );

  /* Upload host resources to Device memory. */
  void uploadToDevice(UploadFlags const flags = kUploadFlagBits_Default);

  /* Construct the image info buffer for the scene textures descriptor set. */
  std::vector<VkDescriptorImageInfo> buildDescriptorImageInfos() const;

  /* Update relevant resources before rendering (eg. shared uniform buffers). */
  void update(Camera const& camera, float elapsed_time);

  /* Render the scene batch per MaterialFx. */
  void render(RenderPassEncoder const& pass);

  // -------------------------------
  void setupRayTracingFx(RayTracingFx* fx); //
  // -------------------------------

 private:
  void uploadImages();

  void uploadBuffers();

  void uploadTransforms();

  void updateFrameData(Camera const& camera, float elapsed_time);

  void prepareRasterizationRendering(Camera const& camera);

 public:
  std::vector<backend::Image> device_images{};
  backend::Buffer vertex_buffer{};
  backend::Buffer index_buffer{};

 protected:
  std::unique_ptr<MaterialFxRegistry> material_fx_registry_{};

  backend::Buffer transforms_sbo_{};
  backend::Buffer frame_sbo_{};

  VkDeviceSize frame_data_stride_{};
  VkDeviceAddress frame_data_current_address_{};

  // -------------------------------
  std::unique_ptr<RayTracingSceneInterface> rt_scene_{};
  RayTracingFx* ray_tracing_fx_{}; //
  // -------------------------------

  using SubMeshBuffer = std::vector<scene::Mesh::SubMesh const*>;
  using FxHashPair = std::pair< MaterialFx*, scene::MaterialStates >;
  using FxHashPairToSubmeshesMap = std::map< FxHashPair, SubMeshBuffer >;
  EnumArray<FxHashPairToSubmeshesMap, scene::MaterialStates::AlphaMode> lookups_{};

 private:
  RenderContext const& context_;

  // [dupplicate, should probably not be stored here]
  uint32_t max_frames_in_flight_{}; //
  uint32_t frame_index_{}; //
};

/* -------------------------------------------------------------------------- */

using GLTFScene = std::shared_ptr<GPUResources>;

#endif // AER_RENDERER_GPU_RESOURCES_H_
