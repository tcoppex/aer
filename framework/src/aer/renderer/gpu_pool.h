#ifndef AER_RENDERER_GPU_POOL_H_
#define AER_RENDERER_GPU_POOL_H_

#include <string_view>
#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"  //~ for backend::Image
#include "aer/scene/asset_handle.h"
#include "aer/scene/geometry.h"
#include "aer/scene/image_data.h"
#include "aer/renderer/device_asset.h"
#include "aer/renderer/fx/material/material_fx_registry.h"

class RenderContext;

namespace scene {
struct Asset;
}

/* -------------------------------------------------------------------------- */

//
//  Manage memory for a scene's device assets (meshes and textures).
//
class GpuPool {
 public:
  struct Settings {
    uint64_t vertex_capacity{};
    uint64_t index_capacity{};
    uint32_t max_textures{};
    bool ray_tracing{};

    // (might migrate elsewhere)
    Geometry::AttributeLocationMap attribute_locations{}; //
  };

 public:
  bool setup(RenderContext const&, MaterialFxRegistry&, Settings const&);
  void release();

  AssetHandle uploadAsset(scene::Asset&&);
  bool releaseAsset(AssetHandle);

  // void beginFrame(uint32_t frame_index);     // (retire garbage)
  // void bindTextures();

  /* Construct the image info buffer for the scene textures descriptor set. */
  // std::vector<VkDescriptorImageInfo> buildDescriptorImageInfos() const;

  [[nodiscard]]
  DeviceAsset const* find(AssetHandle) const;

  [[nodiscard]]
  DeviceAsset* find(AssetHandle);

  [[nodiscard]]
  Settings const& settings() const noexcept { return settings_; }

  [[nodiscard]]
  backend::Buffer const& vertex_buffer() const noexcept { return vertex_buffer_; }

  [[nodiscard]]
  backend::Buffer const& index_buffer() const noexcept { return index_buffer_; }

  [[nodiscard]]
  std::vector<backend::Image> const& images() const noexcept { return images_; }

 private:
  bool uploadImages(
    std::span<scene::ImageData const> images,
    std::vector<uint32_t>& out_slots,        // per host image, kInvalidIndexU32 if unusable
    std::vector<uint32_t>& owned_slots       // appended: slots to free on release
  );

  bool uploadGeometry(
    scene::Asset const& asset,
    std::vector<MeshBufferRange> const& rel,
    DeviceAsset const& da
  );

  void writeTextureDescriptor(
    uint32_t texture_slot,
    uint32_t image_slot,
    scene::Sampler const& sampler
  );

  void freeDeviceResources(DeviceAsset&);

 private:
  RenderContext const* context_ptr_{};
  MaterialFxRegistry * registry_ptr_{};

  Settings settings_{};

  VmaVirtualBlock vertex_vblock_{};
  backend::Buffer vertex_buffer_{};

  backend::Buffer index_buffer_{};
  VmaVirtualBlock index_vblock_{};

  std::vector<backend::Image> images_{};
  VmaVirtualBlock texture_vblock_{};

  // (wip)
  std::vector<uint32_t> free_images_{};
  VmaVirtualAllocation default_texture_alloc_{};
  scene::MaterialProxy::TextureBinding default_binding_{};
  uint32_t fallback_image_slot_{}; // white 1x1

  struct Slot {
    DeviceAsset asset{};
    uint32_t generation{};
    bool alive{};
  };
  std::vector<Slot> slots_{};
  std::vector<uint32_t> free_slots_{};

  // [TODO]
  // -----------------------------
  // FreeList images_;
  // FreeList textures_;
  // FrameGarbage garbage_;
  // -----------------------------
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_GPU_POOL_H_
