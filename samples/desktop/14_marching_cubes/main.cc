/* -------------------------------------------------------------------------- */
//
//    14 - Marching Cubes
//
//
/* -------------------------------------------------------------------------- */

#include <random>

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

namespace shader_interop {
#include "shaders/interop.h"
}

#include "chunk_grid.h"

// ----------------------------------------------------------------------------

class MarchingCubeSample final : public Application {
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
    S.renderer.sample_count = VK_SAMPLE_COUNT_1_BIT;
    S.app_name = "Marching Cubes";
    return S;
  }

  bool setup() final {
    wm_->set_title("14 - Marching Cube");

    renderer_.set_clear_color({ 0.52f, 0.45f, 0.65f, 1.0f });

    /* Setup the ArcBall camera. */
    {
      camera_.makePerspective(
        lina::radians(60.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.1f,
        750.0f
      );
      camera_.set_controller(&arcball_controller_);
      arcball_controller_.set_dolly(5.0f);
    }

    /* Chunk Grid */
    chunk_grid_.setup(context_, uint3(4, 4, 4)); //

    /* Allocate the uniform buffer. */
    {
      // TODO: allocate as a properly padded ring buffer
      uniform_buffer_ = context_.createBuffer(
        sizeof(host_data_), // (* max_frames_in_flight)
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        , VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Allocate Device Buffers. */
    {
      const uint32_t kHeuristicMaxNonEmptyCellsSize = ChunkGrid::kHeuristicChunkMaxVertices
                                                    * sizeof(uint32_t)
                                                    ;

      non_empty_cells_sbo_ = context_.createBuffer(
        "MarchingCubes::Buffer::NonEmptyCells",
        kHeuristicMaxNonEmptyCellsSize,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
      );

      vertices_to_generate_sbo_ = context_.createBuffer(
        "MarchingCubes::Buffer::VerticesToGenerate",
        3u * kHeuristicMaxNonEmptyCellsSize,
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
      );

      atomic_count_sbo_ = context_.createBuffer(
        "MarchingCubes::Buffer::AtomicCount",
        3u * sizeof(uint32_t),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
      );

      indirect_sbo_ = context_.createBuffer(
        2u * (3u * sizeof(uint32_t)),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
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
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
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
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT
      );
    }

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

    // PushConstant base setup.
    {
      auto &pc = push_constant_;

      // pc.gridSize               = uint3();
      // pc.chunkAttributes        = float4();

      pc.nonEmptyCellsBuffer       = non_empty_cells_sbo_.address;
      pc.verticesToGenerateBuffer  = vertices_to_generate_sbo_.address;
      pc.atomicCountBuffer         = atomic_count_sbo_.address;
      pc.indirectBuffer            = indirect_sbo_.address;

      // pc.indicesBuffer             = indices_sbo_.address;
      // pc.verticesBuffer            = vertices_sbo_.address;
    }

    // Setup initial uniform buffer.
    {
      host_data_.viewMatrix       = camera_.view();
      host_data_.projectionMatrix = camera_.proj();
      host_data_.tanFov           = camera_.tan_fovs();
      host_data_.focal            = camera_.focals();
      host_data_.resolution       = float2(
        static_cast<float>(camera_.width()),
        static_cast<float>(camera_.height())
      );
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

  void runMarchingCubePipeline(CommandEncoder const& cmd) {
  }

  void update(float const dt) final {
    host_data_.viewMatrix = camera_.view();
    context_.writeBuffer(uniform_buffer_, host_data_); //
  }

  void draw(CommandEncoder const& cmd) final {
    runMarchingCubePipeline(cmd);

    auto pass = cmd.beginRendering();
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

  backend::Image density_volume_{};
  backend::Image vertex_indices_volume_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  VkPipelineLayout pipeline_layout_{};
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, Compute_kCount> compute_pipelines_{};

  // ----------
};

// ----------------------------------------------------------------------------

ENTRY_POINT(MarchingCubeSample)

/* -------------------------------------------------------------------------- */
