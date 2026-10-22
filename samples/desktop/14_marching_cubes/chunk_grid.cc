#include "chunk_grid.h"

#include <algorithm>
#include <cmath>

#include "aer/core/utils.h"

namespace shader_interop {
#include "shaders/interop.h"
}

// ----------------------------------------------------------------------------

namespace {

constexpr uint32_t kVertexStride = 2u * sizeof(float4);
constexpr uint32_t kHeuristicChunkVerticesBufferSize = shader_interop::kChunkMaxVertices
                                                     * kVertexStride;

constexpr uint32_t kIndexStride  = sizeof(uint32_t);
constexpr uint32_t kHeuristicChunkIndicesBufferSize  = shader_interop::kChunkMaxIndices
                                                     * kIndexStride;

}

// ----------------------------------------------------------------------------

void ChunkGrid::setup(RenderContext const& context) {
  setup(context, Settings{});
}

// ----------------------------------------------------------------------------

void ChunkGrid::setup(RenderContext const& context, Settings const& settings) {
  context_ptr_ = &context;
  settings_ = settings;

  buildLoadOffsets();

  slot_count_ = std::max(1u, settings_.max_slots);

  free_slots_.resize(slot_count_);
  for (uint32_t i = 0u; i < slot_count_; ++i) {
    free_slots_[i] = slot_count_ - 1u - i;
  }

  buffers_.vertex = context.createBuffer(
    "ChunkGrid::VertexBuffer",
    static_cast<VkDeviceSize>(slot_count_) * kHeuristicChunkVerticesBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
  );

  buffers_.index = context.createBuffer(
    "ChunkGrid::IndexBuffer",
    static_cast<VkDeviceSize>(slot_count_) * kHeuristicChunkIndicesBufferSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
  );

  buffers_.draw_indirect = context.createBuffer(
    "ChunkGrid::DrawIndirectBuffer",
    static_cast<VkDeviceSize>(slot_count_) * kDrawIndexedIndirectSize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT //
    | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT
    ,
    VMA_MEMORY_USAGE_AUTO,
    backend::Allocator::kAllocMappedAtCreation
  );
  if (auto &buf = buffers_.draw_indirect; buf.is_mapped()) {
    auto *data = reinterpret_cast<VkDrawIndexedIndirectCommand*>(buf.mapped_data);
    memset(data, 0u, slot_count_ * kDrawIndexedIndirectSize);

    for (size_t i = 0; i < slot_count_; ++i) {
      data[i].instanceCount = 1u;
      data[i].firstIndex    = static_cast<uint32_t>(i * shader_interop::kChunkMaxIndices);

      // (vertices are pulled from a device address in the vertex shader,
      //  using SV_DrawIndex, so vertexOffset has no effect)
    }
    context.flushBuffer(buf);
  }

  LOGD(
    "ChunkGrid: {} slots ({:.1f} MB), {} chunks in the load ellipsoid.",
    slot_count_,
    static_cast<double>(slot_count_)
      * (kHeuristicChunkVerticesBufferSize + kHeuristicChunkIndicesBufferSize)
      / (1024.0 * 1024.0),
    load_offsets_.size()
  );
}

// ----------------------------------------------------------------------------

void ChunkGrid::release() {
  LOG_CHECK(context_ptr_ != nullptr);

  context_ptr_->destroyResources(
    buffers_.vertex,
    buffers_.index,
    buffers_.draw_indirect
  );
  buffers_ = {};

  resident_.clear();
  free_slots_.clear();
  retired_.clear();
  pending_.clear();
  has_center_ = false;
}

// ----------------------------------------------------------------------------

void ChunkGrid::invalidate(uint32_t frame_index) {
  for (auto const& [key, entry] : resident_) {
    if (entry.slot != kNoSlot) {
      retireSlot(entry.slot, frame_index);
    }
  }
  resident_.clear();
  pending_.clear();
  resident_non_empty_ = 0u;
  has_center_ = false;
  out_of_slots_warned_ = false;
}

// ----------------------------------------------------------------------------

void ChunkGrid::update(Camera const& camera, uint32_t frame_index) {
  auto const& view_position = camera.position();
  auto const& frustum       = camera.frustum();

  pending_.clear();
  recycleSlots(frame_index);

  float const inv_size = 1.0f / shader_interop::kChunkSize;
  int3 const center(lina::floor(view_position * inv_size));

  bool const center_changed = !has_center_
                           || (center.x != last_center_.x)
                           || (center.y != last_center_.y)
                           || (center.z != last_center_.z);
  if (center_changed) {
    unloadFarChunks(center, frame_index);
    last_center_ = center;
    has_center_ = true;
  }

  // Load the closest missing chunks.
  for (auto const& offset : load_offsets_) {
    if (pending_.size() >= settings_.build_budget) {
      break;
    }

    int3 const coords(center.x + offset.x, center.y + offset.y, center.z + offset.z);
    uint64_t const key = PackKey(coords);

    if (resident_.find(key) != resident_.end()) {
      continue;
    }

    // Nothing to build : remember it as resident to not test it again.
    if (isOutsideSurfaceSlab(coords)) {
      resident_.emplace(key, Entry{ coords, kNoSlot });
      continue;
    }

    if (free_slots_.empty()) {
      if (!out_of_slots_warned_) {
        LOGW("ChunkGrid: out of slots, the view distance is clamped. "
             "(raise Settings::max_slots or lower the radii)");
        out_of_slots_warned_ = true;
      }
      break;
    }

    uint32_t const slot = free_slots_.back();
    free_slots_.pop_back();

    resident_.emplace(key, Entry{ coords, slot });
    ++resident_non_empty_;
    pending_.push_back(makeChunk(slot, coords));
  }

  if (draw_indirect_dirty_) {
    context_ptr_->flushBuffer(buffers_.draw_indirect);
    draw_indirect_dirty_ = false;
  }
}

// ----------------------------------------------------------------------------

uint64_t ChunkGrid::PackKey(int3 const& c) noexcept {
  // 21 bits per axis (+-1M chunks).
  constexpr int32_t kBias = 1 << 20;
  constexpr uint64_t kMask = (1ull << 21) - 1ull;
  return ((static_cast<uint64_t>(c.x + kBias) & kMask) <<  0)
       | ((static_cast<uint64_t>(c.y + kBias) & kMask) << 21)
       | ((static_cast<uint64_t>(c.z + kBias) & kMask) << 42);
}

// ----------------------------------------------------------------------------

Chunk ChunkGrid::makeChunk(uint32_t slot, int3 const& coords) const {
  float3 const coordsWS = shader_interop::kChunkSize * float3(
    static_cast<float>(coords.x),
    static_cast<float>(coords.y),
    static_cast<float>(coords.z)
  );
  Chunk::Offsets const offsets{
    .vertex         = static_cast<uint32_t>(slot * kHeuristicChunkVerticesBufferSize),
    .index          = static_cast<uint32_t>(slot * kHeuristicChunkIndicesBufferSize),
    .draw_indirect  = static_cast<uint32_t>(slot * kDrawIndexedIndirectSize),
  };
  return Chunk(slot, coords, coordsWS, offsets);
}

// ----------------------------------------------------------------------------

bool ChunkGrid::isOutsideSurfaceSlab(int3 const& coords) const noexcept {
  float const y0 = shader_interop::kChunkSize * static_cast<float>(coords.y);
  float const y1 = y0 + shader_interop::kChunkSize;
  return (y1 < kSurfaceMinY) || (y0 > kSurfaceMaxY);
}

// ----------------------------------------------------------------------------

bool ChunkGrid::isInsideEllipsoid(
  int3 const& delta,
  float radius_xz,
  float radius_y
) const noexcept {
  float const x = static_cast<float>(delta.x) / radius_xz;
  float const y = static_cast<float>(delta.y) / radius_y;
  float const z = static_cast<float>(delta.z) / radius_xz;
  return (x*x + y*y + z*z) <= 1.0f;
}

// ----------------------------------------------------------------------------

void ChunkGrid::buildLoadOffsets() {
  float const rxz = settings_.radius_xz + 0.5f;
  float const ry  = settings_.radius_y + 0.5f;

  int32_t const mxz = static_cast<int32_t>(std::ceil(rxz));
  int32_t const my  = static_cast<int32_t>(std::ceil(ry));

  load_offsets_.clear();
  for (int32_t z = -mxz; z <= mxz; ++z) {
    for (int32_t y = -my; y <= my; ++y) {
      for (int32_t x = -mxz; x <= mxz; ++x) {
        int3 const d(x, y, z);
        if (isInsideEllipsoid(d, rxz, ry)) {
          load_offsets_.push_back(d);
        }
      }
    }
  }

  auto const weight = [](int3 const& d) {
    return static_cast<float>(d.x*d.x + d.z*d.z) + 1.5f * static_cast<float>(d.y*d.y);
  };
  std::stable_sort(
    load_offsets_.begin(), load_offsets_.end(),
    [&](int3 const& a, int3 const& b) { return weight(a) < weight(b); }
  );
}

// ----------------------------------------------------------------------------

void ChunkGrid::unloadFarChunks(int3 const& center, uint32_t frame_index) {
  float const rxz = settings_.radius_xz + 0.5f + settings_.unload_margin;
  float const ry  = settings_.radius_y  + 0.5f + settings_.unload_margin;

  for (auto it = resident_.begin(); it != resident_.end(); ) {
    auto const& entry = it->second;
    int3 const delta(
      entry.coords.x - center.x,
      entry.coords.y - center.y,
      entry.coords.z - center.z
    );
    if (isInsideEllipsoid(delta, rxz, ry)) {
      ++it;
      continue;
    }
    if (entry.slot != kNoSlot) {
      retireSlot(entry.slot, frame_index);
      --resident_non_empty_;
    }
    it = resident_.erase(it);
  }
}

// ----------------------------------------------------------------------------

void ChunkGrid::retireSlot(uint32_t slot, uint32_t frame_index) {
  if (auto &buf = buffers_.draw_indirect; buf.is_mapped()) {
    auto *data = reinterpret_cast<VkDrawIndexedIndirectCommand*>(buf.mapped_data);
    data[slot].indexCount = 0u;
    draw_indirect_dirty_ = true;
  }
  retired_.push_back({ slot, frame_index });
}

// ----------------------------------------------------------------------------

void ChunkGrid::recycleSlots(uint32_t frame_index) {
  while (!retired_.empty()
      && (retired_.front().frame + kSlotRetireDelay <= frame_index)) {
    free_slots_.push_back(retired_.front().slot);
    retired_.pop_front();
  }
}

// ----------------------------------------------------------------------------
