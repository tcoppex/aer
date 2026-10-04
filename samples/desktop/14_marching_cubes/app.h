#pragma once

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "chunk_grid.h"

namespace shader_interop {
#include "shaders/interop.h"
}

// ----------------------------------------------------------------------------

class MarchingCubeSample final : public Application {
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

    Compute_kCount,
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.sample_count = VK_SAMPLE_COUNT_4_BIT;
    S.app_name = "Marching Cubes";
    return S;
  }

  bool setup() final;

  void buildUI() final;

  void release() final;

  void buildChunk(CommandEncoder const& cmd, ChunkGrid::Chunk const& chunk);

  void update(float const dt) final;

  void draw(CommandEncoder const& cmd) final;

 private:
  ArcBallController arcball_controller_{};

  shader_interop::UniformBufferData host_data_{};
  backend::Buffer uniform_buffer_{};

  ChunkGrid chunk_grid_{};

  // ----------

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
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, Compute_kCount> compute_pipelines_{};

  // ----------

  struct {
    VkPipelineLayout layout{};
    Pipeline pipeline{};
  } rendering_;
};

/* -------------------------------------------------------------------------- */
