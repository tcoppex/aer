#include "marching_cubes.h"

#include "chunk_grid.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

namespace {

// Derived Chunk constant values

static_assert(shader_interop::kChunkMaxIndices % 3u == 0u);

static constexpr uint32_t kScratchMaxCells                  = shader_interop::kChunkDim
                                                            * shader_interop::kChunkDim
                                                            * shader_interop::kChunkDim
                                                            ;

static constexpr uint32_t kScratchMaxVertices               = 3u * kScratchMaxCells;

static constexpr uint32_t kHeuristicChunkMaxCells           = kScratchMaxCells; //

static constexpr uint32_t kScratchNonEmptyCellsSize         = kScratchMaxCells
                                                            * sizeof(uint32_t);

static constexpr uint32_t kScratchVerticesToGenerateSize    = kScratchMaxVertices
                                                            * sizeof(uint32_t);
// ----------------------------------------------------------------------------

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

// ----------------------------------------------------------------------------

void MarchingCubes::setup(RenderContext const& context) {
  context_ptr_ = &context;

  constexpr VmaMemoryUsage kDefaultBufferMemoryUsage{
    kEnableDebugRun ? VMA_MEMORY_USAGE_GPU_TO_CPU
                    : VMA_MEMORY_USAGE_GPU_ONLY
  };

  auto & allocator = context.allocator();
  VkDeviceSize const bytes_before = allocator.getTotalAllocationBytes();

  /* Allocate Device Buffers. */
  {
    // TODO: Allocate one large scratch buffer for the whole grid
    //        instead of one per chunk.

    non_empty_cells_sbo_ = context.createBuffer(
      "MarchingCubes::Buffer::NonEmptyCells",
      kScratchNonEmptyCellsSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      kDefaultBufferMemoryUsage
    );

    vertices_to_generate_sbo_ = context.createBuffer(
      "MarchingCubes::Buffer::VerticesToGenerate",
      kScratchVerticesToGenerateSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      kDefaultBufferMemoryUsage
    );

    atomic_count_sbo_ = context.createBuffer(
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

    indirect_sbo_ = context.createBuffer(
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

    density_volume_ = context.createImage(
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
    vertex_indices_volume_ = context.createImage(
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

    context.transitionColorImages(
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
    auto const& SP = context.sampler_pool();

    descriptor_set_layout_ = context.createDescriptorSetLayout({
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

    descriptor_set_ = context.createDescriptorSet(descriptor_set_layout_, {
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
    pipeline_layout_ = context.createPipelineLayout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });

    auto shaders = context.createShaderModules(SAMPLE_SPIRV_DIR, {
      "build_density_volume.slang",
      "list_non_empty_cells.slang",
      "list_vertices_to_generate.slang",
      "splat_vertex_indices.slang",
      "setup_dispatch_indirect.slang",
      "generate_vertices.slang",
      "generate_indices.slang",
      "finalize_chunk.slang"
    });
    context.createComputePipelines(
      pipeline_layout_,
      shaders,
      compute_pipelines_.data()
    );
    context.releaseShaderModules(shaders);
  }
}

// ----------------------------------------------------------------------------

void MarchingCubes::release() {
  if (!context_ptr_) {
    return;
  }

  for (auto pipeline : compute_pipelines_) {
    context_ptr_->destroyPipeline(pipeline);
  }

  context_ptr_->destroyResources(
    non_empty_cells_sbo_,
    vertices_to_generate_sbo_,
    atomic_count_sbo_,
    indirect_sbo_,
    density_volume_,
    vertex_indices_volume_,
    pipeline_layout_,
    descriptor_set_layout_
  );
}

// ----------------------------------------------------------------------------

void MarchingCubes::build(
  ChunkBuffers const& chunk_buffers,
  std::vector<Chunk> const& chunks
) {
  if (chunks.empty()) {
    return;
  }

  // (blocking submission : the budget per frame keeps the stall small)
  auto cmd = context_ptr_->createTransientCommandEncoder(Context::TargetQueue::Compute);
  for (auto const& chunk : chunks) {
    buildChunk(cmd, chunk_buffers, chunk);
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
  context_ptr_->finishTransientCommandEncoder(cmd);
}

// ----------------------------------------------------------------------------

void MarchingCubes::buildChunk(
  CommandEncoder const& cmd,
  ChunkBuffers const& chunk_buffers,
  Chunk const& chunk
) {
  constexpr uint32_t kKernel      = shader_interop::kCompute_BuildDensity_kernelSize;
  uint32_t const kVolumeRes       = static_cast<uint32_t>(shader_interop::kDensityVolumeTexRes);
  constexpr uint32_t kChunkDim    = shader_interop::kChunkDim;
  constexpr VkDeviceSize kUint4   = sizeof(uint4);

  auto const& chunk_offsets = chunk.offsets();

  auto const& atomic_count    = atomic_count_sbo_;
  auto const& non_empty_cells = non_empty_cells_sbo_;
  auto const& to_generate     = vertices_to_generate_sbo_;
  auto const& indirect        = indirect_sbo_;

  // Chunk specific push constants.
  // (ideally, we should avoid using them for better concurrency)
  shader_interop::PushConstant local_pc{
    .chunkAttributes            = float4(chunk.worldspace_coords(), shader_interop::kChunkSize),

    .gridSize                   = uint3(0u),
    .atomicCountIndex           = 0u,
    .atomicCountLimit           = 0u,

    .nonEmptyCellsBuffer        = non_empty_cells_sbo_.address,
    .verticesToGenerateBuffer   = vertices_to_generate_sbo_.address,
    .atomicCountBuffer          = atomic_count_sbo_.address,
    .indirectBuffer             = indirect_sbo_.address,

    .verticesBuffer             = chunk_buffers.vertex.address + chunk_offsets.vertex,
    .indicesBuffer              = chunk_buffers.index.address + chunk_offsets.index,
    .drawIndexedIndirectBuffer  = chunk_buffers.draw_indirect.address + chunk_offsets.draw_indirect,
  };

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
    pc.atomicCountLimit = kHeuristicChunkMaxCells;
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
    pc.atomicCountLimit = shader_interop::kChunkMaxVertices;
  });
  cmd.dispatchIndirect(indirect, indirect_cells_offset_);

  // 5. Setup the indirect dispatch over the vertices (also clamps their count).
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kReadWrite, shader_interop::ATOMIC_COUNT_VERT * sizeof(uint32_t), sizeof(uint32_t)),
  });
  bind(Compute_SetupDispatchIndirect, [&](auto &pc) {
    pc.atomicCountIndex = shader_interop::ATOMIC_COUNT_VERT;
    pc.atomicCountLimit = shader_interop::kChunkMaxVertices;
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
    pc.atomicCountLimit = shader_interop::kChunkMaxVertices; // (vertex budget)
  });
  cmd.dispatchIndirect(indirect, indirect_cells_offset_);

  // 9. Finalize : publish the index count to the draw command, last.
  cmd.pipelineBufferBarriers({
    BufferBarrier(atomic_count, kWrite, kRead),
    BufferBarrier(
      chunk_buffers.draw_indirect, kWrite, kWrite,   // (reset by the density pass)
      chunk_offsets.draw_indirect, ChunkGrid::kDrawIndexedIndirectSize
    ),
  });
  bind(Compute_FinalizeChunk, [](auto &) {});
  cmd.dispatch();
}

/* -------------------------------------------------------------------------- */
