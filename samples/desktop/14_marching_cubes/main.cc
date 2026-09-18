/* -------------------------------------------------------------------------- */
//
//    14 - Marching Cubes
//
//
/* -------------------------------------------------------------------------- */

#include <random>

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

  bool setup() final {
    wm_->set_title("14 - Marching Cubes");

    renderer_.set_clear_color({ 0.52f, 0.45f, 0.65f, 1.0f });

    /* Setup the ArcBall camera. */
    {
      camera_.makePerspective(
        lina::radians(60.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.1f,
        250.0f
      );
      camera_.set_controller(&arcball_controller_);
      arcball_controller_.set_dolly(50.0f);
    }

    /* Chunk Grid */
    chunk_grid_.setup(context_, uint3(4, 4, 4)); //

    /* Allocate the uniform buffer. */
    {
      uniform_buffer_ = context_.createBuffer(
        sizeof(host_data_),
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        , VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    constexpr VmaMemoryUsage kDefaultBufferMemoryUsage{
      kEnableDebugRun ? VMA_MEMORY_USAGE_GPU_TO_CPU
                      : VMA_MEMORY_USAGE_GPU_ONLY
    };


    auto & allocator = context_.allocator();
    VkDeviceSize const bytes_before = allocator.getTotalAllocationBytes();

    /* Allocate Device Buffers. */
    {
      // TODO: Allocate one large scratch buffer for the whole grid
      //        instead of one per chunk.

      non_empty_cells_sbo_ = context_.createBuffer(
        "MarchingCubes::Buffer::NonEmptyCells",
        ChunkGrid::kHeuristicChunkMaxNonEmptyCellsSize,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        kDefaultBufferMemoryUsage
      );

      vertices_to_generate_sbo_ = context_.createBuffer(
        "MarchingCubes::Buffer::VerticesToGenerate",
        ChunkGrid::kHeuristicChunkIndicesBufferSize,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        kDefaultBufferMemoryUsage
      );

      atomic_count_sbo_ = context_.createBuffer(
        "MarchingCubes::Buffer::AtomicCount",
        4u * sizeof(uint32_t),
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

      // [use 'rgba16float' to be able to use filtering, while only needing 'r16float']
      density_volume_ = context_.createImage(
        "MarchingCubes::Texture::DensityVolume",
        VK_IMAGE_VIEW_TYPE_3D,
        VkExtent3D{kVolumeRes, kVolumeRes, kVolumeRes},
        1u,
        1u,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
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
        VK_FORMAT_R32_UINT,
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

    LOGD("MarchingCube buffer usage: {:.2f} MB allocated ({:.2f} MB if extended)",
           delta_mb, delta_mb * chunk_grid_.chunks().size());

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
          .binding = shader_interop::kDescriptorSetBinding_DensityTexture_Storage,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
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
        "generate_indices.slang"
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
      rendering_.layout = context_.createPipelineLayout({
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
          .module = shader.module,
          .entryPoint = "vertexMain",
        },
        .fragment = {
          .module = shader.module,
          .entryPoint = "fragmentMain",
          .targets = {
            {
              .writeMask = VK_COLOR_COMPONENT_R_BIT
                         | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT
                         | VK_COLOR_COMPONENT_A_BIT
                         ,
            }
          },
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
      pc.chunkAttributes            = float4(0.0f);
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

  void buildUI() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Separator();
    }
    ImGui::End();
  }

  void release() final {
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

  void buildChunk(CommandEncoder const& cmd, ChunkGrid::Chunk &chunk) {
    uint32_t const kVolumeWorkGroupSize = shader_interop::kCompute_BuildDensity_kernelSize;

    uint32_t const kVolumeTexRes  = static_cast<uint32_t>(shader_interop::kDensityVolumeTexRes);
    uint32_t const kChunkDim      = shader_interop::kChunkDim;

    auto const& grid_buffers  = chunk_grid_.buffers();
    auto const& chunk_offsets = chunk.offsets();

    // Chunk specific push constants.
    // (ideally, we should avoid using them for better concurrency)
    auto local_pc = push_constant_;
    {
      local_pc.chunkAttributes = float4(chunk.worldspace_coords(), shader_interop::kChunkSize); //
      local_pc.verticesBuffer  = grid_buffers.vertex.address + chunk_offsets.vertex;
      local_pc.indicesBuffer   = grid_buffers.index.address + chunk_offsets.index;
      local_pc.drawIndexedIndirectBuffer = grid_buffers.draw_indirect.address + chunk_offsets.draw_indirect;
    }

    // Bind the shared descriptor set.
    cmd.bindDescriptorSet(descriptor_set_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

    // Clear shared resources.
    {
      // (perform in the BuildDensity shader)
      // cmd.fillBuffer(atomic_count_sbo_, 0); //

      // (should not be needed for static meshes)
      // cmd.clearColorImage(vertex_indices_volume_, float4(0.0f));
    }

    // -----------------------

    // 1. Build Density Volume.
    {
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = atomic_count_sbo_.buffer,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
          .srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = grid_buffers.draw_indirect.buffer,
          .offset        = chunk_offsets.draw_indirect,
          .size          = ChunkGrid::kDrawIndexedIndirectSize,
        },
      });
      cmd.pipelineImageBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_NONE,
          .srcAccessMask = VK_ACCESS_2_NONE,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
          .newLayout = VK_IMAGE_LAYOUT_GENERAL,
          .image = density_volume_.image,
          .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        },
      });

      cmd.bindPipeline(compute_pipelines_[Compute_BuildDensityVolume]);

      auto pc = local_pc;
      pc.gridSize = uint3(kVolumeTexRes);
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.runKernel<kVolumeWorkGroupSize, kVolumeWorkGroupSize, kVolumeWorkGroupSize>(
        kVolumeTexRes, kVolumeTexRes, kVolumeTexRes
      );
    }

    // 2. List non empty cells.
    {
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = atomic_count_sbo_.buffer,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = non_empty_cells_sbo_.buffer,
        },
      });
      cmd.pipelineImageBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
          .newLayout = VK_IMAGE_LAYOUT_GENERAL,
          .image = density_volume_.image,
          .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        },
      });

      cmd.bindPipeline(compute_pipelines_[Compute_ListNonEmptyCells]);

      auto pc = local_pc;
      pc.gridSize = uint3(kChunkDim);
      pc.atomicCountLimit = ChunkGrid::kHeuristicChunkMaxCells;
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.runKernel<kVolumeWorkGroupSize, kVolumeWorkGroupSize, kVolumeWorkGroupSize>(
        kChunkDim, kChunkDim, kChunkDim
      );
    }

    // 3. Setup Indirect Cells.
    {
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = atomic_count_sbo_.buffer,
          .offset        = shader_interop::ATOMIC_COUNT_CELL * sizeof(uint32_t),
          .size          = sizeof(uint32_t),
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
          .srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = indirect_sbo_.buffer,
        },
      });

      cmd.bindPipeline(compute_pipelines_[Compute_SetupDispatchIndirect]);

      auto pc = local_pc;
      pc.atomicCountIndex = shader_interop::ATOMIC_COUNT_CELL;
      pc.indirectBuffer   = indirect_sbo_.address + indirect_cells_offset_;
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch();
    }

    // 4. List Vertices
    {
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                         | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = atomic_count_sbo_.buffer,
          // .offset        = ATOMIC_COUNT_CELL | ATOMIC_COUNT_VERT
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = vertices_to_generate_sbo_.buffer,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = non_empty_cells_sbo_.buffer,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
          .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
          .buffer        = indirect_sbo_.buffer,
          .offset        = indirect_cells_offset_,
          .size          = sizeof(uint4),
        },
      });
      cmd.bindPipeline(compute_pipelines_[Compute_ListVertices]);

      auto pc = local_pc;
      pc.atomicCountLimit = ChunkGrid::kHeuristicChunkMaxVertices;
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatchIndirect(indirect_sbo_, indirect_cells_offset_);
    }

    // 5. Setup Indirect Vertices
    {
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
          .buffer        = atomic_count_sbo_.buffer,
          .offset        = shader_interop::ATOMIC_COUNT_VERT * sizeof(uint32_t),
          .size          = sizeof(uint32_t),
        },
      });

      cmd.bindPipeline(compute_pipelines_[Compute_SetupDispatchIndirect]);

      auto pc = local_pc;
      pc.atomicCountIndex = shader_interop::ATOMIC_COUNT_VERT;
      pc.indirectBuffer   = indirect_sbo_.address + indirect_vertices_offset_;
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch();
    }

    // 6. Splat Vertex Indices.
    cmd.pipelineBufferBarriers({
      {
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .buffer        = vertices_to_generate_sbo_.buffer,
      },
      {
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
        .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
        .buffer        = indirect_sbo_.buffer,
        .offset        = indirect_vertices_offset_,
        .size          = sizeof(uint4),
      },
    });
    cmd.pipelineImageBarriers({
      {
        .srcStageMask  = VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = vertex_indices_volume_.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
      },
    });
    cmd.bindPipeline(compute_pipelines_[Compute_SplatVertexIndices]);
    cmd.dispatchIndirect(indirect_sbo_, indirect_vertices_offset_);

    // -----------

    // 7. Generate Vertices.
    cmd.pipelineBufferBarriers({
      {
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        .buffer        = vertices_to_generate_sbo_.buffer,
      },
      {
        .srcStageMask  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT, //
        .srcAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, //
        .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .buffer        = grid_buffers.vertex.buffer,
        .offset        = chunk_offsets.vertex,
        .size          = ChunkGrid::kHeuristicChunkVerticesBufferSize,
      }
    });
    cmd.bindPipeline(compute_pipelines_[Compute_GenerateVertices]);
    cmd.dispatchIndirect(indirect_sbo_, indirect_vertices_offset_);

    // 8. Generate Indices.
    {
      cmd.pipelineBufferBarriers({
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = grid_buffers.draw_indirect.buffer,
          .offset        = chunk_offsets.draw_indirect,
          .size          = ChunkGrid::kDrawIndexedIndirectSize,
        },
        {
          .srcStageMask  = VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT, //
          .srcAccessMask = VK_ACCESS_2_INDEX_READ_BIT, //
          .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
          .buffer        = grid_buffers.index.buffer,
          .offset        = chunk_offsets.index,
          .size          = ChunkGrid::kHeuristicChunkIndicesBufferSize,
        },
      });
      cmd.bindPipeline(compute_pipelines_[Compute_GenerateIndices]);

      auto pc = local_pc;
      pc.atomicCountLimit = ChunkGrid::kHeuristicChunkIndicesBufferSize;
      cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatchIndirect(indirect_sbo_, indirect_cells_offset_);
    }
  }

  void update(float const dt) final {
    host_data_.viewMatrix = camera_.view();
    context_.writeBuffer(uniform_buffer_, host_data_); //

    // -------

    if (frame_index() > 0) {
      return;
    }

    auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Compute);
    for (auto &chunk : chunk_grid_.chunks()) {
      buildChunk(cmd, chunk);

      // (wip)
      cmd.pipelineMemoryBarrier({
        .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                       | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                       | VK_ACCESS_2_INDEX_READ_BIT
                       | VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
      });
    }
    context_.finishTransientCommandEncoder(cmd);
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      auto const& grid_buffers = chunk_grid_.buffers();

      pass.bindPipeline(rendering_.pipeline);
      // pass.bindIndexBuffer(grid_buffers.index);

      shader_interop::PushConstant_Rendering pc{
        .modelMatrix  = float4x4(lina::identity),
        .uniformData  = reinterpret_cast<shader_interop::UniformBufferData*>(uniform_buffer_.address),
        .vertices     = reinterpret_cast<shader_interop::Vertex*>(grid_buffers.vertex.address)
      };

      // -----------------------
      for (auto const& chunk : chunk_grid_.chunks()) {
        auto const& offsets = chunk.offsets();
        uint64_t const vertices_addr = grid_buffers.vertex.address + offsets.vertex;

        pc.vertices = reinterpret_cast<shader_interop::Vertex*>(vertices_addr);
        pass.pushConstant(pc, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

        pass.bindIndexBuffer(
          grid_buffers.index,
          VK_INDEX_TYPE_UINT32,
          offsets.index
        );

        pass.drawIndexedIndirect(grid_buffers.draw_indirect, offsets.draw_indirect);
      }
      // -----------------------

      // To render every chunk at once.
      // pass.drawIndexedIndirect(grid_buffers.draw_indirect, 0u, chunk_grid_.size(), ChunkGrid::kDrawIndirectStride);
    }
    cmd.endRendering();

    drawUI(cmd);
  }

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

// ----------------------------------------------------------------------------

ENTRY_POINT(MarchingCubeSample)

/* -------------------------------------------------------------------------- */
