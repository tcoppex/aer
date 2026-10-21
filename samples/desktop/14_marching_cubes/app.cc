#include <random>

#include "app.h"

/* -------------------------------------------------------------------------- */

namespace {

// Synchronization helpers

constexpr VkPipelineStageFlags2 kStageCompute  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
constexpr VkPipelineStageFlags2 kStageIndirect = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

constexpr VkAccessFlags2 kRead      = VK_ACCESS_2_SHADER_READ_BIT;
constexpr VkAccessFlags2 kWrite     = VK_ACCESS_2_SHADER_WRITE_BIT;
constexpr VkAccessFlags2 kReadWrite = kRead | kWrite;
constexpr VkAccessFlags2 kIndirect  = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;

VkBufferMemoryBarrier2 BufferBarrier(
  backend::Buffer const& buffer,
  VkAccessFlags2 src,
  VkAccessFlags2 dst,
  VkDeviceSize offset = 0u,
  VkDeviceSize size = VK_WHOLE_SIZE
) {
  VkBufferMemoryBarrier2 barrier{};
  barrier.srcStageMask  = kStageCompute;
  barrier.srcAccessMask = src;
  barrier.dstStageMask  = kStageCompute;
  barrier.dstAccessMask = dst;
  barrier.buffer        = buffer.buffer;
  barrier.offset        = offset;
  barrier.size          = size;
  return barrier;
}

VkBufferMemoryBarrier2 IndirectBarrier(
  backend::Buffer const& buffer,
  VkDeviceSize offset = 0u,
  VkDeviceSize size = VK_WHOLE_SIZE
) {
  auto barrier = BufferBarrier(buffer, kWrite, kIndirect, offset, size);
  barrier.dstStageMask = kStageIndirect;
  return barrier;
}

VkBufferMemoryBarrier2 IndirectReuseBarrier(
  backend::Buffer const& buffer,
  VkDeviceSize offset = 0u,
  VkDeviceSize size = VK_WHOLE_SIZE
) {
  auto barrier = BufferBarrier(buffer, kIndirect, kWrite, offset, size);
  barrier.srcStageMask = kStageIndirect;
  return barrier;
}

VkImageMemoryBarrier2 ImageBarrier(
  backend::Image const& image,
  VkAccessFlags2 src,
  VkAccessFlags2 dst
) {
  VkImageMemoryBarrier2 barrier{};
  barrier.srcStageMask  = kStageCompute;
  barrier.srcAccessMask = src;
  barrier.dstStageMask  = kStageCompute;
  barrier.dstAccessMask = dst;
  barrier.oldLayout     = VK_IMAGE_LAYOUT_GENERAL;
  barrier.newLayout     = VK_IMAGE_LAYOUT_GENERAL;
  barrier.image         = image.image;
  return barrier;
}

} // namespace

/* -------------------------------------------------------------------------- */

bool MarchingCubeSample::setup() {
  wm_->set_title("14 - Marching Cubes");

  renderer_.set_clear_color({ 0.52f, 0.45f, 0.65f, 1.0f });

  /* Setup the camera. */
  {
    camera_.makePerspective(
      lina::radians(60.0f),
      viewport_size_.width,
      viewport_size_.height,
      0.1f,
      600.0f
    );
    camera_.set_controller(&fly_controller_);
    fly_controller_.set_position(vec3(0.0f, 8.0f, 48.0f));
  }

  {
    auto &skybox = renderer_.skybox();

    skybox.setup(ASSETS_DIR "textures/"
      "scythian_tombs_puresky_2k.hdr"
    );
    skybox.set_hdr_intensity(0.25f);
  }

  constexpr VmaMemoryUsage kDefaultBufferMemoryUsage{
    kEnableDebugRun ? VMA_MEMORY_USAGE_GPU_TO_CPU
                    : VMA_MEMORY_USAGE_GPU_ONLY
  };

  auto & allocator = context_.allocator();
  /* Chunk Grid */
  {
    VkDeviceSize const bytes_before = allocator.getTotalAllocationBytes();

    chunk_grid_.setup(context_);

    VkDeviceSize const bytes_after = allocator.getTotalAllocationBytes();
    VkDeviceSize const net_allocation_delta = bytes_after - bytes_before;
    double const delta_mb = static_cast<double>(net_allocation_delta) / (1024.0 * 1024.0);

    LOGD("ChunkGrid buffer usage: {:.2f} MB allocated.", delta_mb);
  }

  /* Allocate the uniform buffer. */
  {
    uniform_buffer_ = context_.createBuffer(
      sizeof(host_data_),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      , VMA_MEMORY_USAGE_CPU_TO_GPU
    );
  }

  VkDeviceSize const bytes_before = allocator.getTotalAllocationBytes();

  /* Allocate Device Buffers. */
  {
    // TODO: Allocate one large scratch buffer for the whole grid
    //        instead of one per chunk.

    non_empty_cells_sbo_ = context_.createBuffer(
      "MarchingCubes::Buffer::NonEmptyCells",
      ChunkGrid::kScratchNonEmptyCellsSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      kDefaultBufferMemoryUsage
    );

    vertices_to_generate_sbo_ = context_.createBuffer(
      "MarchingCubes::Buffer::VerticesToGenerate",
      ChunkGrid::kScratchVerticesToGenerateSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      kDefaultBufferMemoryUsage
    );

    atomic_count_sbo_ = context_.createBuffer(
      "MarchingCubes::Buffer::AtomicCount",
      4u * sizeof(uint32_t), //
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      ,
      kDefaultBufferMemoryUsage
    );

    uint32_t const kIndirectDispatchSize = sizeof(uint4);
    indirect_cells_offset_    = 0u * kIndirectDispatchSize;
    indirect_vertices_offset_ = 1u * kIndirectDispatchSize;

    indirect_sbo_ = context_.createBuffer(
      2u * kIndirectDispatchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
      kDefaultBufferMemoryUsage
    );
  }

  /* Allocate Device Images. */
  {
    auto const kVolumeRes = static_cast<uint32_t>(shader_interop::kDensityVolumeTexRes);

    density_volume_ = context_.createImage(
      "MarchingCubes::Texture::DensityVolume",
      VK_IMAGE_VIEW_TYPE_3D,
      VkExtent3D{kVolumeRes, kVolumeRes, kVolumeRes},
      1u,
      1u,
      VK_SAMPLE_COUNT_1_BIT,
      VK_FORMAT_R16_SFLOAT,
        VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_STORAGE_BIT
    );

    // [use 3*width RED instead of RGB values to avoid concurrent texel store operations]
    // [might want to switch to 'r16uint']
    vertex_indices_volume_ = context_.createImage(
      "MarchingCubes::Texture::VertexIndicesVolume",
      VK_IMAGE_VIEW_TYPE_3D,
      VkExtent3D{3u * kVolumeRes, kVolumeRes, kVolumeRes},
      1u,
      1u,
      VK_SAMPLE_COUNT_1_BIT,
      VK_FORMAT_R32_UINT, //
        VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_STORAGE_BIT
      | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    );

    context_.transitionColorImages(
      {density_volume_, vertex_indices_volume_},
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_GENERAL
    );
  }

  VkDeviceSize const bytes_after = allocator.getTotalAllocationBytes();
  VkDeviceSize const net_allocation_delta = bytes_after - bytes_before;
  double const delta_mb = static_cast<double>(net_allocation_delta) / (1024.0 * 1024.0);

  LOGD("Scratch buffer usage: {:.2f} MB allocated.", delta_mb);

  /* Descriptor set. */
  {
    auto const& SP = context_.sampler_pool();

    descriptor_set_layout_ = context_.createDescriptorSetLayout({
      {
        .binding = shader_interop::kDescriptorSetBinding_SamplerNearest,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_SamplerLinear,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_DensityTexture_Sampling,
        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_DensityTexture_Storage,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_VertexIndicesVolume,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
    });

    descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
      {
        .binding = shader_interop::kDescriptorSetBinding_SamplerNearest,
        .type = VK_DESCRIPTOR_TYPE_SAMPLER,
        .images = {
          {
            .sampler = SP.anyso_clampedge_nearest(),
          }
        }
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_SamplerLinear,
        .type = VK_DESCRIPTOR_TYPE_SAMPLER,
        .images = {
          {
            .sampler = SP.anyso_clampedge_linear(),
          }
        }
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_DensityTexture_Sampling,
        .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .images = {
          {
            .imageView = density_volume_.view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          }
        }
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_DensityTexture_Storage,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .images = {
          {
            .imageView = density_volume_.view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          }
        }
      },
      {
        .binding = shader_interop::kDescriptorSetBinding_VertexIndicesVolume,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .images = {
          {
            .imageView = vertex_indices_volume_.view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          }
        }
      },
    });
  }

  /* Create the Compute Pipelines */
  {
    pipeline_layout_ = context_.createPipelineLayout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .size = sizeof(push_constant_),
        }
      },
    });

    auto shaders = context_.createShaderModules(SAMPLE_SPIRV_DIR, {
      "build_density_volume.slang",
      "list_non_empty_cells.slang",
      "list_vertices_to_generate.slang",
      "splat_vertex_indices.slang",
      "setup_dispatch_indirect.slang",
      "generate_vertices.slang",
      "generate_indices.slang",
      "finalize_chunk.slang"
    });
    context_.createComputePipelines(
      pipeline_layout_,
      shaders,
      compute_pipelines_.data()
    );
    context_.releaseShaderModules(shaders);
  }

  // ------------------------------------

  {
    auto const& ds_scene = context_.descriptor_registry()
      .descriptor(DescriptorRegistry::Type::Scene);

    rendering_.layout = context_.createPipelineLayout({
      .setLayouts = { ds_scene.layout },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_FRAGMENT_BIT,
          .size = sizeof(shader_interop::PushConstant_Rendering),
        }
      },
    });

    auto shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "rendering.slang");

    rendering_.pipeline = context_.createGraphicsPipeline(rendering_.layout, {
      .vertex = {
        .module = shader.handle,
        .entryPoint = "vertexMain",
      },
      .fragment = {
        .module = shader.handle,
        .entryPoint = "fragmentMain",
        .targets = { GraphicsPipelineDescriptor_t::Fragment::Target{} },
      },
      .depthStencil = {
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      },
      .primitive = {
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .cullMode = VK_CULL_MODE_BACK_BIT,
      }
    });

    context_.releaseShaderModule(shader);
  }

  // ------------------------------------

  // PushConstant base setup.
  {
    auto &pc = push_constant_;
    auto const& chunk_grid_buffer = chunk_grid_.buffers();

    // (to be updated per-stage needing them)
    pc.gridSize                   = uint3(0u);
    pc.atomicCountIndex           = 0u;
    pc.atomicCountLimit           = 0u; //

    // (to be updated per-chunk)
    pc.chunkAttributes            = float4(0.0f); //
    pc.verticesBuffer             = chunk_grid_buffer.vertex.address;
    pc.indicesBuffer              = chunk_grid_buffer.index.address;
    pc.drawIndexedIndirectBuffer  = chunk_grid_buffer.draw_indirect.address;

    // (fixed)
    pc.nonEmptyCellsBuffer        = non_empty_cells_sbo_.address;
    pc.verticesToGenerateBuffer   = vertices_to_generate_sbo_.address;
    pc.atomicCountBuffer          = atomic_count_sbo_.address;
    pc.indirectBuffer             = indirect_sbo_.address;
  }

  // Setup initial uniform buffer.
  {
    host_data_.viewMatrix       = camera_.view();
    host_data_.projectionMatrix = camera_.proj();
    context_.writeBuffer(uniform_buffer_, host_data_);
  }

  return true;
}

// ----------------------------------------------------------------------------

void MarchingCubeSample::buildUI() {
  ImGui::Begin("Settings");
  {
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Separator();

    ImGui::TextUnformatted("RMB + mouse: look | WASD: move | E/Q: up/down");
    ImGui::TextUnformatted("Shift: faster | Ctrl: slower");
    ImGui::SliderFloat("Speed", fly_controller_.speed_ptr(), 2.0f, 400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::Separator();

    ImGui::Text("Chunks: %u resident, %u pending", chunk_grid_.resident_count(), static_cast<uint32_t>(chunk_grid_.pending().size()));
    ImGui::Text("Slots: %u free / %u (%u retiring)", chunk_grid_.free_slot_count(), chunk_grid_.size(), chunk_grid_.retired_slot_count());

    int budget = static_cast<int>(chunk_grid_.settings().build_budget);
    if (ImGui::SliderInt("Build budget / frame", &budget, 1, 64)) {
      chunk_grid_.settings().build_budget = static_cast<uint32_t>(budget);
    }
    if (ImGui::Button("Rebuild world")) {
      chunk_grid_.invalidate(frame_index());
    }
  }
  ImGui::End();
}

// ----------------------------------------------------------------------------

void MarchingCubeSample::release() {
  for (auto pipeline : compute_pipelines_) {
    context_.destroyPipeline(pipeline);
  }
  context_.destroyResources(
    rendering_.pipeline,
    rendering_.layout,

    non_empty_cells_sbo_,
    vertices_to_generate_sbo_,
    atomic_count_sbo_,
    indirect_sbo_,

    density_volume_,
    vertex_indices_volume_,

    pipeline_layout_,
    uniform_buffer_,
    descriptor_set_layout_
  );

  chunk_grid_.release();
}

// ----------------------------------------------------------------------------

void MarchingCubeSample::buildChunk(CommandEncoder const& cmd, ChunkGrid::Chunk const& chunk) {
  constexpr uint32_t kKernel      = shader_interop::kCompute_BuildDensity_kernelSize;
  uint32_t const kVolumeRes       = static_cast<uint32_t>(shader_interop::kDensityVolumeTexRes);
  constexpr uint32_t kChunkDim    = shader_interop::kChunkDim;
  constexpr VkDeviceSize kUint4   = sizeof(uint4);

  auto const& grid_buffers  = chunk_grid_.buffers();
  auto const& chunk_offsets = chunk.offsets();

  auto const& atomic_count    = atomic_count_sbo_;
  auto const& non_empty_cells = non_empty_cells_sbo_;
  auto const& to_generate     = vertices_to_generate_sbo_;
  auto const& indirect        = indirect_sbo_;

  // Chunk specific push constants.
  // (ideally, we should avoid using them for better concurrency)
  auto local_pc = push_constant_;
  local_pc.chunkAttributes           = float4(chunk.worldspace_coords(), shader_interop::kChunkSize);
  local_pc.verticesBuffer            = grid_buffers.vertex.address + chunk_offsets.vertex;
  local_pc.indicesBuffer             = grid_buffers.index.address + chunk_offsets.index;
  local_pc.drawIndexedIndirectBuffer = grid_buffers.draw_indirect.address + chunk_offsets.draw_indirect;

  auto const bind = [&](Compute stage, auto const& customize) {
    auto pc = local_pc;
    customize(pc);
    cmd.bindPipeline(compute_pipelines_[stage]);
    cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);
  };

  cmd.bindDescriptorSet(descriptor_set_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

  // NOTE : the output ranges of the chunk (vertices, indices, draw command)
  //        belong to a slot which is not in use by any frame in flight (see
  //        ChunkGrid::kSlotRetireDelay), so they need no barrier here.

  // 1. Build the density volume (also resets the per-chunk counters).
  cmd.pipelineBufferBarriers({ BufferBarrier(atomic_count, kReadWrite, kWrite) });
  cmd.pipelineImageBarriers({ ImageBarrier(density_volume_, kRead, kWrite) });
  bind(Compute_BuildDensityVolume, [&](auto &pc) {
    pc.gridSize = uint3(kVolumeRes);
  });
  cmd.runKernel<kKernel, kKernel, kKernel>(kVolumeRes, kVolumeRes, kVolumeRes);

  // 2. List the non empty cells.
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kReadWrite),
    BufferBarrier(non_empty_cells, kRead, kWrite),
  });
  cmd.pipelineImageBarriers({
    ImageBarrier(density_volume_, kWrite, kRead),
  });
  bind(Compute_ListNonEmptyCells, [&](auto &pc) {
    pc.gridSize         = uint3(kChunkDim);
    pc.atomicCountLimit = ChunkGrid::kHeuristicChunkMaxCells;
  });
  cmd.runKernel<kKernel, kKernel, kKernel>(kChunkDim, kChunkDim, kChunkDim);

  // 3. Setup the indirect dispatch over the cells.
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kRead, shader_interop::ATOMIC_COUNT_CELL * sizeof(uint32_t), sizeof(uint32_t)),
    IndirectReuseBarrier(indirect),                  // (previous chunk)
  });
  bind(Compute_SetupDispatchIndirect, [&](auto &pc) {
    pc.atomicCountIndex = shader_interop::ATOMIC_COUNT_CELL;
    pc.indirectBuffer   = indirect.address + indirect_cells_offset_;
  });
  cmd.dispatch();

  // 4. List the vertices to generate (clamped to the chunk budget).
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kReadWrite, kReadWrite),
    BufferBarrier(to_generate, kRead, kWrite),
    BufferBarrier(non_empty_cells, kWrite, kRead),
    IndirectBarrier(indirect, indirect_cells_offset_, kUint4),
  });
  bind(Compute_ListVertices, [&](auto &pc) {
    pc.atomicCountLimit = ChunkGrid::kHeuristicChunkMaxVertices;
  });
  cmd.dispatchIndirect(indirect, indirect_cells_offset_);

  // 5. Setup the indirect dispatch over the vertices (also clamps their count).
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kReadWrite, shader_interop::ATOMIC_COUNT_VERT * sizeof(uint32_t), sizeof(uint32_t)),
  });
  bind(Compute_SetupDispatchIndirect, [&](auto &pc) {
    pc.atomicCountIndex = shader_interop::ATOMIC_COUNT_VERT;
    pc.atomicCountLimit = ChunkGrid::kHeuristicChunkMaxVertices;
    pc.indirectBuffer   = indirect.address + indirect_vertices_offset_;
  });
  cmd.dispatch();

  // 6. Splat the vertex indices into the volume.
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kRead),      // (clamped vertex count)
    BufferBarrier(to_generate, kWrite, kRead),
    IndirectBarrier(indirect, indirect_vertices_offset_, kUint4),
  });
  cmd.pipelineImageBarriers({
    ImageBarrier(vertex_indices_volume_, kReadWrite, kWrite)
  });
  bind(Compute_SplatVertexIndices, [](auto &) {});
  cmd.dispatchIndirect(indirect, indirect_vertices_offset_);

  // 7. Generate the vertices.
  cmd.pipelineBufferBarriers({
    BufferBarrier(to_generate, kWrite, kRead),
  });
  bind(Compute_GenerateVertices, [](auto &) {});
  cmd.dispatchIndirect(indirect, indirect_vertices_offset_);

  // 8. Generate the indices (their count is accumulated in the atomic buffer).
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kReadWrite, kReadWrite),
  });
  cmd.pipelineImageBarriers({
    ImageBarrier(vertex_indices_volume_, kWrite, kRead),
  });
  bind(Compute_GenerateIndices, [&](auto &pc) {
    pc.atomicCountLimit = ChunkGrid::kHeuristicChunkMaxVertices; // (vertex budget)
  });
  cmd.dispatchIndirect(indirect, indirect_cells_offset_);

  // 9. Finalize : publish the index count to the draw command, last.
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kRead),
    BufferBarrier(
      grid_buffers.draw_indirect, kWrite, kWrite,   // (reset by the density pass)
      chunk_offsets.draw_indirect, ChunkGrid::kDrawIndexedIndirectSize
    ),
  });
  bind(Compute_FinalizeChunk, [](auto &) {});
  cmd.dispatch();
}

// ----------------------------------------------------------------------------

void MarchingCubeSample::update(float const dt) {
  host_data_.viewMatrix = camera_.view();
  context_.writeBuffer(uniform_buffer_, host_data_); //

  // -------

  // Stream the world around the camera.
  chunk_grid_.update(fly_controller_.position(), frame_index());

  auto const& pending = chunk_grid_.pending();
  if (pending.empty()) {
    return;
  }

  // (blocking submission : the budget per frame keeps the stall small)
  auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);
  for (auto const& chunk : pending) {
    buildChunk(cmd, chunk);
  }

  // Make the results visible to the rendering.
  cmd.pipelineMemoryBarrier({
    .srcStageMask  = kStageCompute,
    .srcAccessMask = kWrite,
    .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                   | VK_ACCESS_2_INDEX_READ_BIT
                   | VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
  });
  context_.finishTransientCommandEncoder(cmd);
}

// ----------------------------------------------------------------------------

void MarchingCubeSample::draw(CommandEncoder const& cmd) {
  auto pass = cmd.beginRendering();
  {
    if (auto const& skybox = renderer_.skybox(); skybox.is_valid()) {
      skybox.render(pass, camera_);
    }

    auto const& grid_buffers = chunk_grid_.buffers();

    pass.bindPipeline(rendering_.pipeline);
    pass.pushConstant(shader_interop::PushConstant_Rendering{
        .modelMatrix  = float4x4(lina::identity),
        .uniformData  = reinterpret_cast<shader_interop::UniformBufferData*>(uniform_buffer_.address),
        .vertices     = reinterpret_cast<shader_interop::MarchingCubeVertex*>(grid_buffers.vertex.address),
        .vertexOffset = ChunkGrid::kHeuristicChunkMaxVertices,
      },
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    );
    pass.bindIndexBuffer(grid_buffers.index, VK_INDEX_TYPE_UINT32);

    auto const& ds = context_.descriptor_registry().descriptor(
      DescriptorRegistry::Type::Scene
    );
    pass.bindDescriptorSet(ds.set, VK_SHADER_STAGE_FRAGMENT_BIT);

    pass.drawIndexedIndirect(
      grid_buffers.draw_indirect, 0u, chunk_grid_.size(), ChunkGrid::kDrawIndexedIndirectSize
    );

  }
  cmd.endRendering();

  drawUI(cmd);
}

/* -------------------------------------------------------------------------- */
