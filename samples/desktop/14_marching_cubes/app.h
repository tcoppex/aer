#pragma once

#include "aer/application.h"
#include "aer/core/fly_view_controller.h"

#include "marching_cubes.h"
#include "chunk_grid.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
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

  void update(float const dt) final;

  void draw(CommandEncoder const& cmd) final;

 private:
  FlyViewController fly_controller_{};

  struct {
    backend::Buffer uniform_buffer{};
    VkPipelineLayout layout{};
    Pipeline pipeline{};
  } rendering_;

  MarchingCubes generator_{};
  ChunkGrid chunk_grid_{};
};

/* -------------------------------------------------------------------------- */
