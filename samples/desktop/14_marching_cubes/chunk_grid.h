#ifndef MARCHINGCUBES_CHUNK_GRID_H_
#define MARCHINGCUBES_CHUNK_GRID_H_

#include "aer/core/common.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/render_context.h"

// ----------------------------------------------------------------------------

class ChunkGrid {
 public:
  // [should be updated depending on kChunkDim, and density function complexity]
  static constexpr uint32_t kHeuristicChunkMaxVertices  = (1u << 13u); // 4096
  static constexpr uint32_t kHeuristicChunkMaxIndices   = (1u << 16u); // 32768

  // -----------------------

  static constexpr uint32_t kChunkStride = sizeof(float4);
  static constexpr uint32_t kVertexStride = (4u + 4u) * sizeof(float);

  static constexpr uint32_t kHeuristicChunkVerticesBufferSize = kHeuristicChunkMaxVertices
                                                              * kVertexStride
                                                              ;

  static constexpr uint32_t kHeuristicChunkIndicesBufferSize = kHeuristicChunkMaxIndices
                                                             * sizeof(uint32_t)
                                                             ;

  static constexpr uint32_t kDrawIndexedIndirectSize = 5u * sizeof(uint32_t);

  // -----------------------

 public:
  class Chunk {
   public:
    struct Offsets {
      uint32_t vertex{};
      uint32_t index{};
      uint32_t draw_indirect{};
      // uint32_t chunk{};
    };

   public:
    Chunk(
      uint32_t index,
      uint3 const& coords,
      float3 const& coordsWS,
      Chunk::Offsets const& offsets
    )
    : index_(index)
    , coords_(coords)
    , coordsWS_(coordsWS)
    , offsets_(offsets)
    {}

    [[nodiscard]]
    Offsets const& offsets() const noexcept { return offsets_; }

    [[nodiscard]]
    float3 const& worldspace_coords() const noexcept { return coordsWS_; }

   private:
    uint32_t index_{};
    uint3 coords_{};
    float3 coordsWS_{};
    Offsets offsets_{};
  };

  struct Buffers {
    backend::Buffer vertex{};
    backend::Buffer index{};
    backend::Buffer draw_indirect{};
    // backend::Buffer chunk{};

    [[nodiscard]]
    bool allocated() const noexcept { return vertex.buffer != VK_NULL_HANDLE; }
  };

 public:
  ChunkGrid() = default;

  void setup(RenderContext const& context, uint3 const& dimension);

  void release();

  void draw(RenderPassEncoder const& pass) const;

  [[nodiscard]]
  std::vector<Chunk>& chunks() noexcept { return chunks_; }

  [[nodiscard]]
  Buffers const& buffers() noexcept { return buffers_; }

 private:
  void reset(uint3 const& dimension);

 private:
  RenderContext const* context_ptr_{};

  uint3 dimension_{};
  size_t size_{};

  std::vector<Chunk> chunks_{};

  Buffers buffers_{};
};

// ----------------------------------------------------------------------------

#endif // MARCHINGCUBES_CHUNK_GRID_H_