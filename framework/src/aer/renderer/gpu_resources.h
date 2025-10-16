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

struct GPUResources : scene::HostResources {
 public:
  static bool constexpr kReleaseHostDataOnUpload = true;

 public:
  GPUResources(RenderContext const& context);

  ~GPUResources();

  /* Load a scene assets from disk to Host memory. */
  bool loadFile(std::string_view filename);

  /* Bind mesh attributes to pipeline locations. */
  void initializeSubmeshDescriptors(
    scene::Mesh::AttributeLocationMap const& attribute_to_location
  );

  /* Upload host resources to Device memory. */
  void uploadToDevice(
    bool const bReleaseHostDataOnUpload = kReleaseHostDataOnUpload
  );

  /* Construct the image info buffer for the scene textures descriptor set. */
  std::vector<VkDescriptorImageInfo> buildDescriptorImageInfos() const;

  /* Update relevant resources before rendering (eg. shared uniform buffers). */
  void update(
    Camera const& camera,
    VkExtent2D const& surface_size,
    float elapsed_time
  );

  /* Render the scene batch per MaterialFx. */
  void render(RenderPassEncoder const& pass);

  // -------------------------------
  void set_ray_tracing_fx(RayTracingFx* fx); //
  // -------------------------------

 private:
  void uploadImages();
  void uploadBuffers();

  void updateFrameData(
    Camera const& camera,
    VkExtent2D const& surface_size,
    float elapsed_time
  );

 public:
  /* --- Device Data --- */

  std::vector<backend::Image> device_images{};
  backend::Buffer vertex_buffer{};
  backend::Buffer index_buffer{};

 protected:
  backend::Buffer frame_ubo_{};
  backend::Buffer transforms_ssbo_{};

  std::unique_ptr<MaterialFxRegistry> material_fx_registry_{};

  // -------------------------------
  std::unique_ptr<RayTracingSceneInterface> rt_scene_{};
  RayTracingFx const* ray_tracing_fx_{};
  // -------------------------------

  using SubMeshBuffer = std::vector<scene::Mesh::SubMesh const*>;
  using FxHashPair = std::pair< MaterialFx*, scene::MaterialStates >;
  using FxHashPairToSubmeshesMap = std::map< FxHashPair, SubMeshBuffer >;
  EnumArray<FxHashPairToSubmeshesMap, scene::MaterialStates::AlphaMode> lookups_{};

 private:
  RenderContext const& context_;
  uint32_t frame_index_{};
};

/* -------------------------------------------------------------------------- */

using GLTFScene = std::shared_ptr<GPUResources>;

#endif // AER_RENDERER_GPU_RESOURCES_H_
