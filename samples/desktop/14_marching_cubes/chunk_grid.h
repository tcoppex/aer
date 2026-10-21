#ifndef MARCHINGCUBES_CHUNK_GRID_H_
#define MARCHINGCUBES_CHUNK_GRID_H_

#include <deque>
#include <unordered_map>
#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/render_context.h"

#include "chunk_types.h"

// ----------------------------------------------------------------------------

//
// Streams chunks around a view position.
//
class ChunkGrid {
 private:
  static constexpr float kSurfaceMinY = -64.0f;
  static constexpr float kSurfaceMaxY = 96.0f;

 public:
  static constexpr uint32_t kDrawIndexedIndirectSize = sizeof(VkDrawIndexedIndirectCommand);
  static constexpr uint32_t kSlotRetireDelay = 4u;
  static constexpr uint32_t kNoSlot = ~0u;

 public:
  struct Settings {
    float radius_xz{ 8.0f };
    float radius_y{ 4.0f };
    float unload_margin{ 1.5f };
    uint32_t max_slots{ 1536u };
    uint32_t build_budget{ 64u };
  };

 public:
  ChunkGrid() = default;

  void setup(RenderContext const& context);
  void setup(RenderContext const& context, Settings const& settings);

  void release();

  void update(float3 const& view_position, uint32_t frame_index);

  void invalidate(uint32_t frame_index);

  [[nodiscard]]
  std::vector<Chunk> const& pending() const noexcept { return pending_; }

  [[nodiscard]]
  ChunkBuffers const& buffers() const noexcept { return buffers_; }

  [[nodiscard]]
  uint32_t size() const noexcept { return slot_count_; }

  [[nodiscard]]
  Settings & settings() noexcept { return settings_; }

  [[nodiscard]] uint32_t resident_count() const noexcept { return resident_non_empty_; }
  [[nodiscard]] uint32_t free_slot_count() const noexcept { return static_cast<uint32_t>(free_slots_.size()); }
  [[nodiscard]] uint32_t retired_slot_count() const noexcept { return static_cast<uint32_t>(retired_.size()); }

 private:
  struct Entry {
    int3 coords{};
    uint32_t slot{ kNoSlot };
  };

  struct Retired {
    uint32_t slot{};
    uint32_t frame{};
  };

  static uint64_t PackKey(int3 const& c) noexcept;

  [[nodiscard]]
  Chunk makeChunk(uint32_t slot, int3 const& coords) const;

  [[nodiscard]]
  bool isOutsideSurfaceSlab(int3 const& coords) const noexcept;

  [[nodiscard]]
  bool isInsideEllipsoid(int3 const& delta, float radius_xz, float radius_y) const noexcept;

  void buildLoadOffsets();

  void unloadFarChunks(int3 const& center, uint32_t frame_index);

  void retireSlot(uint32_t slot, uint32_t frame_index);

  void recycleSlots(uint32_t frame_index);

 private:
  RenderContext const* context_ptr_{};

  Settings settings_{};
  uint32_t slot_count_{};

  // (sorted from the closest to the farthest)
  std::vector<int3> load_offsets_{};

  std::unordered_map<uint64_t, Entry> resident_{};
  std::vector<uint32_t> free_slots_{};
  std::deque<Retired> retired_{};
  std::vector<Chunk> pending_{};

  int3 last_center_{};
  bool has_center_{};
  bool draw_indirect_dirty_{};
  bool out_of_slots_warned_{};
  uint32_t resident_non_empty_{};

  ChunkBuffers buffers_{};
};

// ----------------------------------------------------------------------------

#endif // MARCHINGCUBES_CHUNK_GRID_H_
