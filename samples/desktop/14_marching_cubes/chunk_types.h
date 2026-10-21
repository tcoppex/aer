#ifndef MARCHINGCUBES_CHUNK_TYPES_H_
#define MARCHINGCUBES_CHUNK_TYPES_H_

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

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
    Offsets const& offsets
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

// ----------------------------------------------------------------------------

struct ChunkBuffers {
  backend::Buffer vertex{};
  backend::Buffer index{};
  backend::Buffer draw_indirect{};

  [[nodiscard]]
  bool allocated() const noexcept { return vertex.buffer != VK_NULL_HANDLE; }
};

/* -------------------------------------------------------------------------- */

#endif // MARCHINGCUBES_CHUNK_TYPES_H_