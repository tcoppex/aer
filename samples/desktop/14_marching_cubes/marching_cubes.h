#pragma once

#include "aer/renderer/render_context.h"
#include "chunk_types.h"

/* -------------------------------------------------------------------------- */

class MarchingCubes {
 public:
  static constexpr bool kEnableDebugRun = false;

 public:
  enum Compute {
    Compute_BuildDensityVolume,
    Compute_ListNonEmptyCells,
    Compute_ListVertices,
    Compute_SplatVertexIndices,
    Compute_SetupDispatchIndirect,
    Compute_GenerateVertices,
    Compute_GenerateIndices,
    Compute_FinalizeChunk,
    Compute_kCount,
  };

 public:
  MarchingCubes() = default;

  void setup(RenderContext const& context);

  void release();

  /* Build a batch of chunks sharing the same buffers. */
  void build(
    ChunkBuffers const& chunk_buffers,
    std::vector<Chunk> const& chunks
  );

 private:
  /* Build individual chunk. */
  void buildChunk(
    CommandEncoder const& cmd,
    ChunkBuffers const& chunk_buffers,
    Chunk const& chunk
  );

 private:
  RenderContext const* context_ptr_{};

  backend::Buffer non_empty_cells_sbo_{};
  backend::Buffer vertices_to_generate_sbo_{};
  backend::Buffer atomic_count_sbo_{};
  backend::Buffer indirect_sbo_{};

  VkDeviceSize indirect_cells_offset_{};
  VkDeviceSize indirect_vertices_offset_{};

  backend::Image density_volume_{};
  backend::Image vertex_indices_volume_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  VkPipelineLayout pipeline_layout_{};

  std::array<Pipeline, Compute_kCount> compute_pipelines_{};
};

/* -------------------------------------------------------------------------- */
