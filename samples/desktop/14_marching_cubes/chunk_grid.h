#ifndef MARCHINGCUBES_CHUNK_GRID_H_
#define MARCHINGCUBES_CHUNK_GRID_H_

#include <deque>
#include <unordered_map>
#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/render_context.h"

// ----------------------------------------------------------------------------

//
// Streams chunks around a view position.
//
class ChunkGrid {
 public:
  // [must match shaders/interop.h, checked in chunk_grid.cc]
  static constexpr uint32_t kChunkDim = 24u;

  // Theoretical maxima of the scratch buffers, one chunk at a time.
  static constexpr uint32_t kScratchMaxCells    = kChunkDim * kChunkDim * kChunkDim;
  static constexpr uint32_t kScratchMaxVertices = 3u * kScratchMaxCells;

  // Per-chunk output budgets (GPU-side clamped).
  static constexpr uint32_t kHeuristicChunkMaxCells    = kScratchMaxCells;
  static constexpr uint32_t kHeuristicChunkMaxVertices = (1u << 14u);
  static constexpr uint32_t kHeuristicChunkMaxIndices  = (1u << 16u) - 1u;

  // -----------------------

  static constexpr uint32_t kVertexStride     = 2u * sizeof(float4); //
  static constexpr uint32_t kIndexStride      = sizeof(uint32_t);

  static constexpr uint32_t kScratchNonEmptyCellsSize = kScratchMaxCells
                                                      * sizeof(uint32_t);

  static constexpr uint32_t kScratchVerticesToGenerateSize = kScratchMaxVertices
                                                           * sizeof(uint32_t);

  static constexpr uint32_t kHeuristicChunkVerticesBufferSize = kHeuristicChunkMaxVertices
                                                              * kVertexStride;

  static constexpr uint32_t kHeuristicChunkIndicesBufferSize = kHeuristicChunkMaxIndices
                                                             * kIndexStride;

  static constexpr uint32_t kDrawIndexedIndirectSize = sizeof(VkDrawIndexedIndirectCommand);

  // -----------------------

  static constexpr uint32_t kSlotRetireDelay = 4u;

  static constexpr uint32_t kNoSlot = ~0u;

 public:
  struct Settings {
    float radius_xz{ 8.0f };
    float radius_y{ 4.0f };
    float unload_margin{ 1.5f };
    uint32_t max_slots{ 1536u };
    uint32_t build_budget{ 8u };
  };

  class Chunk {
   public:
    struct Offsets {
      uint32_t vertex{};
      uint32_t index{};
      uint32_t draw_indirect{};
    };

   public:
    Chunk(
      uint32_t slot,
      int3 const& coords,
      float3 const& coordsWS,
      Chunk::Offsets const& offsets
    )
    : slot_(slot)
    , coords_(coords)
    , coordsWS_(coordsWS)
    , offsets_(offsets)
    {}

    [[nodiscard]]
    Offsets const& offsets() const noexcept { return offsets_; }

    [[nodiscard]]
    float3 const& worldspace_coords() const noexcept { return coordsWS_; }

    [[nodiscard]]
    int3 const& coords() const noexcept { return coords_; }

    [[nodiscard]]
    uint32_t slot() const noexcept { return slot_; }

   private:
    uint32_t slot_{};
    int3 coords_{};
    float3 coordsWS_{};
    Offsets offsets_{};
  };

  struct Buffers {
    backend::Buffer vertex{};
    backend::Buffer index{};
    backend::Buffer draw_indirect{};

    [[nodiscard]]
    bool allocated() const noexcept { return vertex.buffer != VK_NULL_HANDLE; }
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
  Buffers const& buffers() const noexcept { return buffers_; }

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

  Buffers buffers_{};
};

// ----------------------------------------------------------------------------

#endif // MARCHINGCUBES_CHUNK_GRID_H_
