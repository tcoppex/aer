#ifndef AER_RENDERER_DEVICE_ASSET_H_
#define AER_RENDERER_DEVICE_ASSET_H_

#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"  //~ for DrawDescriptor
#include "aer/scene/material.h"

#include "aer/scene/asset.h" // (needed for DeviceAsset::root_transform())

class MaterialFx;

/* -------------------------------------------------------------------------- */

struct BufferRange {
  uint64_t offset{};
  uint64_t size{};
};

struct MeshBufferRange {
  BufferRange vertex{};
  BufferRange index{};
};

struct DeviceSubmesh {
  DrawDescriptor draw_descriptor{};
  uint32_t asset_material_index{};
};

struct DeviceMesh {
  MeshBufferRange buffer_range{};
  VkPrimitiveTopology topology{};
  std::vector<DeviceSubmesh> submeshes{};
  // [TODO] BLAS (for ray tracing)
};

// ----------------------------------------------------------------------------

struct DeviceMaterial {
  MaterialFx* fx{};
  scene::MaterialStates states{};
  uint32_t material_index{};        // slot in the fx's own buffer
  bool double_sided{};
};

// ----------------------------------------------------------------------------

struct DeviceAsset {
  MeshBufferRange buffer_range{};

  VmaVirtualAllocation vertex_alloc{};
  VmaVirtualAllocation index_alloc{};
  VmaVirtualAllocation texture_alloc{};

  std::vector<DeviceMesh>     meshes{};
  std::vector<DeviceMaterial> materials{};
  std::vector<uint32_t>       image_slots{};

  // uint32_t first_material, material_count;
  uint32_t first_texture{};
  uint32_t texture_count{};

  std::shared_ptr<const scene::Prefab> prefab{}; // shared with scene::Asset
  uint32_t instance_count{};

  [[nodiscard]]
  auto const& root_transform() const { return prefab->nodes[0].transform; }
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_DEVICE_ASSET_H_
