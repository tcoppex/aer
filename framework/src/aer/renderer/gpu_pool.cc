#include "aer/renderer/gpu_pool.h"

#include "aer/scene/asset.h"
#include "aer/renderer/render_context.h"
#include "aer/renderer/vertex_layout.h"
#include "aer/renderer/fx/material/material_fx.h"

/* -------------------------------------------------------------------------- */

namespace {

constexpr uint64_t kVertexAlignment = 16u; //
constexpr uint64_t kIndexAlignment  = 4u; //

}

/* -------------------------------------------------------------------------- */

bool GpuPool::setup(
  RenderContext const& context,
  MaterialFxRegistry& fx_registry,
  Settings const& settings
) {
  LOG_CHECK(settings.vertex_capacity > 0);

  context_ptr_ = &context;
  registry_ptr_ = &fx_registry;
  settings_ = settings;

  VkBufferUsageFlags extra_flags{};

  if (settings.ray_tracing) {
    extra_flags = 0
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      ;
  }

  auto const& allocator = context.allocator();
  auto const max_alloc_size = context.max_buffer_allocation_size();
  auto const vbuffer_size = std::min(max_alloc_size, settings.vertex_capacity);
  auto const ibuffer_size = std::min(max_alloc_size, settings.index_capacity);

  vertex_buffer_ = allocator.createBuffer(
    vbuffer_size,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    | extra_flags
    ,
    VMA_MEMORY_USAGE_GPU_ONLY
  );
  vertex_vblock_ = allocator.createVirtualBlock(vbuffer_size);

  if (ibuffer_size > 0ul) {
    index_buffer_ = allocator.createBuffer(
      ibuffer_size,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      | extra_flags
      ,
      VMA_MEMORY_USAGE_GPU_ONLY
    );
    index_vblock_ = allocator.createVirtualBlock(ibuffer_size);
  }

  if (settings_.max_textures > 0u) {
    texture_vblock_ = allocator.createVirtualBlock(settings_.max_textures); //
  }

  // ---------------------
  // Default textures binding setup.
  constexpr uint32_t kNumDefaultTextures = 5u;
  if (settings_.max_textures >= kNumDefaultTextures)
  {
    std::vector<scene::ImageData> defaults{};
    defaults.reserve(kNumDefaultTextures);
    defaults.emplace_back(255, 255, 255, 255);  // basecolor
    defaults.emplace_back(128, 128, 255, 255);  // normal
    defaults.emplace_back(255, 255, 255, 255);  // occlusion
    defaults.emplace_back(  0,   0,   0,   0);  // emissive
    defaults.emplace_back(  0, 255,   0,   0);  // roughness_metallic

    std::vector<uint32_t> image_slot{}, owned{};
    VkDeviceSize first{};
    if (!uploadImages(defaults, image_slot, owned)
     || !allocator.allocateVirtual(texture_vblock_, kNumDefaultTextures, 1u, &default_texture_alloc_, &first)) {
      return false;
    }

    fallback_image_slot_ = image_slot[0];
    scene::Sampler const default_sampler{};
    uint32_t first_u32 = static_cast<uint32_t>(first);
    for (uint32_t i = 0; i < kNumDefaultTextures; ++i) {
      writeTextureDescriptor(first_u32 + i, image_slot[i], default_sampler);
    }
    default_binding_ = {
      .basecolor          = first_u32 + 0,
      .normal             = first_u32 + 1,
      .occlusion          = first_u32 + 2,
      .emissive           = first_u32 + 3,
      .roughness_metallic = first_u32 + 4,
    };
  }
  // ---------------------

  return vertex_buffer_.valid();
}

// ----------------------------------------------------------------------------

void GpuPool::release() {
  if (!context_ptr_) {
    return;
  }
  context_ptr_->deviceWaitIdle();

  auto const& allocator = context_ptr_->allocator();

  for (auto& slot : slots_) {
    if (slot.alive) {
      freeDeviceResources(slot.asset);
      slot.alive = false;
    }
  }

  if (default_texture_alloc_ != VK_NULL_HANDLE) {
    allocator.freeVirtual(texture_vblock_, default_texture_alloc_);
  }

  for (auto& img : images_) {
    if (img.valid()) {
      context_ptr_->destroyImage(img);
    }
  }

  allocator.destroyVirtualBlock(texture_vblock_);
  allocator.destroyVirtualBlock(vertex_vblock_);
  allocator.destroyVirtualBlock(index_vblock_);
  allocator.destroyBuffer(vertex_buffer_);
  allocator.destroyBuffer(index_buffer_);

  *this = {};
}

// ----------------------------------------------------------------------------

AssetHandle GpuPool::uploadAsset(scene::Asset&& asset) {
  if (!context_ptr_ || !registry_ptr_) {
    LOGE("GpuPool::uploadAsset called before setup.");
    return {};
  }
  auto const& allocator = context_ptr_->allocator();

  DeviceAsset da{
    .prefab = asset.prefab
  };

  auto fail = [&](char const* reason) {
    LOGE("GpuPool::uploadAsset: {}", reason);
    freeDeviceResources(da);
    return AssetHandle{};
  };

  // 1. Layout every mesh inside one vertex range and one index range (relative offsets).
  std::vector<MeshBufferRange> rel(asset.meshes.size());
  uint64_t vsize = 0u, isize = 0u;
  for (size_t i = 0; i < asset.meshes.size(); ++i) {
    auto const& mesh = asset.meshes[i];
    vsize = utils::AlignTo(vsize, kVertexAlignment);
    isize = utils::AlignTo(isize, kIndexAlignment);
    rel[i].vertex = { vsize, mesh.vertices_bytesize() };
    rel[i].index  = { isize, mesh.indices_bytesize() };
    vsize += mesh.vertices_bytesize();
    isize += mesh.indices_bytesize();
  }

  // 2. Reserve space (vertices, indices, texture slots). Overflow -> invalid handle.
  VkDeviceSize voffset = 0u, ioffset = 0u, toffset = 0u;
  if (vsize > 0u && !allocator.allocateVirtual(vertex_vblock_, vsize, kVertexAlignment, &da.vertex_alloc, &voffset)) {
    return fail("vertex buffer capacity exceeded.");
  }
  if (isize > 0u && !allocator.allocateVirtual(index_vblock_, isize, kIndexAlignment, &da.index_alloc, &ioffset)) {
    return fail("index buffer capacity exceeded.");
  }
  uint32_t const tsize = static_cast<uint32_t>(asset.textures.size());
  if (tsize > 0u && !allocator.allocateVirtual(texture_vblock_, tsize, 1u, &da.texture_alloc, &toffset)) {
    return fail("texture slots exhausted.");
  }
  da.buffer_range   = {
    .vertex = {voffset, vsize},
    .index  = {ioffset, isize}
  };
  da.first_texture  = static_cast<uint32_t>(toffset);
  da.texture_count  = tsize;

  // 3. Images (device copies). Empty/failed host images get no slot.
  std::vector<uint32_t> image_slot{};
  if (!uploadImages(asset.images, image_slot, da.image_slots)) {
    return fail("image upload failed.");
  }

  // 4. Rebase material texture bindings (if none fallback to default 'sentinel')
  for (auto& proxy : asset.materials) {
    auto rebase = [&](uint32_t& b, uint32_t fallback) {
      b = (b == kInvalidIndexU32) ? fallback : da.first_texture + b;
    };
    auto& b = proxy.bindings;
    rebase(b.basecolor,          default_binding_.basecolor);
    rebase(b.normal,             default_binding_.normal);
    rebase(b.occlusion,          default_binding_.occlusion);
    rebase(b.emissive,           default_binding_.emissive);
    rebase(b.roughness_metallic, default_binding_.roughness_metallic);
  }

  // 5. Device meshes, absolute offsets.
  // (previously initializeSubmeshDescriptors)
  auto const& locations = settings_.attribute_locations;
  da.meshes.resize(asset.meshes.size());
  for (size_t i = 0; i < asset.meshes.size(); ++i) {
    auto const& mesh = asset.meshes[i];
    auto& dm = da.meshes[i];

    dm.buffer_range = {
      .vertex = { voffset + rel[i].vertex.offset, rel[i].vertex.size },
      .index = { ioffset + rel[i].index.offset,  rel[i].index.size  },
    };
    dm.topology = ToVkPrimitiveTopology(mesh.topology());

    for (uint32_t p = 0; p < mesh.primitive_count(); ++p) {
      auto const& prim = mesh.primitive(p);
      auto const pos = prim.bufferOffsets.find(Geometry::AttributeType::Position);
      if (pos == prim.bufferOffsets.end() || p >= mesh.primitive_materials_index.size()) {
        return fail("malformed mesh (no position or material index).");
      }
      uint32_t const material = mesh.primitive_materials_index[p];
      if (material >= asset.materials.size()) {
        return fail("primitive references an unknown material.");
      }

      dm.submeshes.push_back({
        .draw_descriptor = {
          .vertexInput = CreateVertexInputDescriptor(
            mesh,
            dm.buffer_range.vertex.offset,
            prim.bufferOffsets,
            locations
          ),
          .indexType     = ToVkIndexType(mesh.index_format()),
          .indexOffset   = dm.buffer_range.index.offset + prim.indexOffset,
          .vertexOffset  = dm.buffer_range.vertex.offset + pos->second,
          .indexCount    = prim.indexCount,
          .vertexCount   = prim.vertexCount,
          .instanceCount = 1u,
        },
        .asset_material_index = material,
      });
    }
  }

  // 6. Copy geometry to the device.
  if (!uploadGeometry(asset, rel, da)) {
    return fail("geometry upload failed.");
  }

  // 7. Texture descriptors.
  for (uint32_t t = 0; t < tsize; ++t) {
    auto const& tex = asset.textures[t];
    uint32_t slot = (tex.host_image_index < image_slot.size())
                  ? image_slot[tex.host_image_index]
                  : kInvalidIndexU32;
    if (slot == kInvalidIndexU32) {
      slot = fallback_image_slot_;
    }
    writeTextureDescriptor(da.first_texture + t, slot, tex.sampler);
  }

  // 8. Materials.
  {
    if (auto const model = context_ptr_->default_material_model();
        model != scene::MaterialModel::Unknown) {
      for (auto& ref : asset.material_refs) {
        ref.model = model;
      }
    }
    auto const entries = registry_ptr_->add(asset.materials, asset.material_refs);
    if (entries.size() != asset.material_refs.size()) {
      return fail("material registration failed.");
    }
    da.materials.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
      auto const& ref = asset.material_refs[i];
      da.materials.push_back({
        .fx = entries[i].fx,
        .states = ref.states,
        .material_index = entries[i].material_index,
        .double_sided = asset.materials[ref.proxy_index].double_sided,
      });
    }
  }

  // 9. Publish in a slot.
  uint32_t index;
  if (!free_slots_.empty()) {
    index = free_slots_.back();
    free_slots_.pop_back();
  } else {
    index = static_cast<uint32_t>(slots_.size());
    slots_.emplace_back();
  }
  auto& slot = slots_[index];
  slot.asset = std::move(da);
  slot.alive = true;

  return AssetHandle{ index, slot.generation };
}

// ----------------------------------------------------------------------------

bool GpuPool::releaseAsset(AssetHandle h) {
  // -------------------------------------------------------------
  // When an asset is deleted, it isn't immediately wiped or re-indexed.
  // Instead, its slot is marked dead, its generation is incremented
  // (invalidating any old handles out in the wild), and its index is
  // pushed back onto free_slots_ for future recycling.
  // -------------------------------------------------------------

  auto* da = find(h);

  if (!da) {
    LOGW("GpuPool::releaseAsset: stale or invalid handle.");
    return false;
  }

  if (da->instance_count > 0u) {
    LOGE("GpuPool::releaseAsset: {} instance(s) still alive.", da->instance_count);
    return false;
  }

  // Simple and safe for now (TODO: "FrameGarbage" to replaces this later).
  context_ptr_->deviceWaitIdle();
  freeDeviceResources(*da);   // frees ranges + images, resets *da

  auto& slot = slots_[h.index];
  slot.alive = false;
  ++slot.generation;
  free_slots_.push_back(h.index);

  return true;
}

// ----------------------------------------------------------------------------

DeviceAsset const* GpuPool::find(AssetHandle h) const {
  if (!h.valid() || h.index >= slots_.size()) {
    return nullptr;
  }
  auto const& slot = slots_[h.index];
  return (slot.alive && slot.generation == h.generation) ? &slot.asset : nullptr;
}

DeviceAsset* GpuPool::find(AssetHandle h) {
  return const_cast<DeviceAsset*>(std::as_const(*this).find(h));
}

/* -------------------------------------------------------------------------- */

bool GpuPool::uploadImages(
  std::span<scene::ImageData const> images,
  std::vector<uint32_t>& out_slots,
  std::vector<uint32_t>& owned_slots
) {
  out_slots.assign(images.size(), kInvalidIndexU32);

  struct Item {
    uint32_t host;
    uint32_t slot;
    uint64_t staging_offset;
    VkExtent3D extent;
  };
  std::vector<Item> items{};
  std::vector<backend::Image> created{};
  uint64_t staging_size = 0u;

  for (uint32_t i = 0; i < images.size(); ++i) {
    auto const& img = images[i];
    if (!img.pixels() || img.width <= 0 || img.height <= 0) {
      continue;
    }

    VkExtent3D const extent{ uint32_t(img.width), uint32_t(img.height), 1u };
    auto device_image = context_ptr_->createImage2D(
      extent.width,
      extent.height,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );

    uint32_t slot;
    if (!free_images_.empty()) {
      slot = free_images_.back();
      free_images_.pop_back();
      images_[slot] = device_image;
    } else {
      slot = uint32_t(images_.size());
      images_.push_back(device_image);
    }

    out_slots[i] = slot;
    owned_slots.push_back(slot);
    created.push_back(device_image);
    items.push_back({ i, slot, staging_size, extent });
    staging_size += utils::AlignTo(uint64_t(img.bytesize()), 16u);
  }
  if (items.empty()) {
    return true;
  }

  std::vector<VkBufferImageCopy> copies{};
  auto staging = context_ptr_->createStagingBuffer(staging_size);
  {
    std::byte* dst{};
    context_ptr_->mapMemory(staging, (void**)&dst);
    for (auto const& it : items) {
      auto const& img = images[it.host];
      std::memcpy(dst + it.staging_offset, img.pixels(), img.bytesize());
      copies.push_back({
        .bufferOffset = it.staging_offset,
        .imageSubresource = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .layerCount = 1u
        },
        .imageExtent = it.extent,
      });
    }
    context_ptr_->unmapMemory(staging);
  }

  auto cmd = context_ptr_->createTransientCommandEncoder(Context::TargetQueue::Transfer);
  {
    VkImageLayout const transfer = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    cmd.transitionColorImages(created, VK_IMAGE_LAYOUT_UNDEFINED, transfer, 1u);
    for (size_t i = 0; i < items.size(); ++i) {
      vkCmdCopyBufferToImage(
        cmd.handle(), staging.buffer, created[i].image, transfer, 1u, &copies[i]
      );
    }
    cmd.transitionColorImages(created, transfer, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1u);
  }
  context_ptr_->finishTransientCommandEncoder(cmd);

  return true;
}

// ----------------------------------------------------------------------------

bool GpuPool::uploadGeometry(
  scene::Asset const& asset,
  std::vector<MeshBufferRange> const& rel,
  DeviceAsset const& da
) {
  uint64_t const vsize = da.buffer_range.vertex.size;
  uint64_t const isize = da.buffer_range.index.size;
  if (vsize == 0u) {
    return true;
  }

  // Staging layout: [vertices][indices], each mesh at its relative offset.
  auto staging = context_ptr_->createStagingBuffer(vsize + isize);
  {
    std::byte* dst{};
    context_ptr_->mapMemory(staging, (void**)&dst);
    for (size_t i = 0; i < asset.meshes.size(); ++i) {
      auto const& mesh = asset.meshes[i];
      std::memcpy(
        dst + rel[i].vertex.offset, mesh.vertices().data(), mesh.vertices_bytesize()
      );
      if (isize > 0u) {
        std::memcpy(
          dst + vsize + rel[i].index.offset, mesh.indices().data(), mesh.indices_bytesize()
        );
      }
    }
    context_ptr_->unmapMemory(staging);
  }

  auto cmd = context_ptr_->createTransientCommandEncoder(Context::TargetQueue::Transfer);
  {
    cmd.copyBufferToBuffer(staging, 0u, vertex_buffer_, da.buffer_range.vertex.offset, vsize);
    std::vector<VkBufferMemoryBarrier2> barriers{{
      .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
      .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      .buffer = vertex_buffer_.buffer,
      .offset = da.buffer_range.vertex.offset,
      .size = vsize,
    }};
    if (isize > 0u) {
      cmd.copyBufferToBuffer(staging, vsize, index_buffer_, da.buffer_range.index.offset, isize);
      barriers.push_back({
        .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_INDEX_READ_BIT,
        .buffer = index_buffer_.buffer,
        .offset = da.buffer_range.index.offset,
        .size = isize,
      });
    }
    cmd.pipelineBufferBarriers(barriers);
  }
  context_ptr_->finishTransientCommandEncoder(cmd);

  return true;
}

// ----------------------------------------------------------------------------

void GpuPool::writeTextureDescriptor(
  uint32_t texture_slot,
  uint32_t image_slot,
  scene::Sampler const& sampler
) {
  HLOGW("GpuPool::writeTextureDescriptor : can probably do better");

  context_ptr_->descriptor_registry().updateSceneTexture(texture_slot, {
    .sampler = context_ptr_->sampler_pool().convert(sampler),
    .imageView = images_[image_slot].view,
    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  });
}

// ----------------------------------------------------------------------------

void GpuPool::freeDeviceResources(DeviceAsset& da) {
  auto const& allocator = context_ptr_->allocator();
  if (da.vertex_alloc  != VK_NULL_HANDLE) {
    allocator.freeVirtual(vertex_vblock_,  da.vertex_alloc);
  }
  if (da.index_alloc   != VK_NULL_HANDLE) {
    allocator.freeVirtual(index_vblock_,   da.index_alloc);
  }
  if (da.texture_alloc != VK_NULL_HANDLE) {
    allocator.freeVirtual(texture_vblock_, da.texture_alloc);
  }
  for (uint32_t slot : da.image_slots) {
    context_ptr_->destroyImage(images_[slot]);
    images_[slot] = {};
    free_images_.push_back(slot);
  }
  da = {};
}

/* -------------------------------------------------------------------------- */
