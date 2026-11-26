#ifndef AER_RENDERER_DEVICE_ASSET_H_
#define AER_RENDERER_DEVICE_ASSET_H_

#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"  //~ for DrawDescriptor, AccelerationStructure
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

  /* byte offset in the pool vertex buffer of this primitives's SkinVertex array. */
  uint64_t skin_vertex_offset{kInvalidIndexU64};

  /* byte offset in the pool vertex buffer of this primitive's morph deltas. */
  uint64_t morph_offset{kInvalidIndexU64};
  uint32_t morph_count{};

  [[nodiscard]]
  bool has_skin() const noexcept { return skin_vertex_offset != kInvalidIndexU64; }

  [[nodiscard]]
  bool has_morph() const noexcept { return morph_count > 0u && morph_offset != kInvalidIndexU64; }

  [[nodiscard]]
  bool is_deformable() const noexcept { return has_skin() || has_morph(); }
};

struct DeviceMesh {
  MeshBufferRange buffer_range{};
  VkPrimitiveTopology topology{};
  std::vector<DeviceSubmesh> submeshes{};

  backend::AccelerationStructure blas{};

  [[nodiscard]]
  bool has_blas() const noexcept { return blas.handle != VK_NULL_HANDLE; }
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
