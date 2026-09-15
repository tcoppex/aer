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

// ----------------------------------------------------------------------------

class MarchingCubeSample final : public Application {
 public:
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

    renderer_.set_clear_color({ 0.2f, 0.75f, 0.5f, 1.0f });

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

    // constexpr VmaMemoryUsage kDefaultBufferMemoryUsage{
    //   kEnableDebugRun ? VMA_MEMORY_USAGE_GPU_TO_CPU
    //                   : VMA_MEMORY_USAGE_GPU_ONLY
    // };

    /* Allocate device buffers. */
    {
      // gaussian_sbo_ = context_.transientCreateBuffer(
      //   gaussians,
      //     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      //   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      // );

      // splat_sbo_ = context_.createBuffer(
      //   gaussians_count_ * sizeof(shader_interop::SplatOutput),
      //     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      //   | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      //   kDefaultBufferMemoryUsage
      // );
    }


    /* Descriptor set. */
    {
      descriptor_set_layout_ = context_.createDescriptorSetLayout({
        {
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        }
      });
      descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
        {
          // .binding = 0,
          // .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          // .images = {
          //   {
          //     .imageView = gs_image_.view,
          //     .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          //   }
          // }
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

      // pc.numElems               = gaussians_count_;

      // pc.uniform_addr           = uniform_buffer_.address;
      // pc.gaussian_addr          = gaussian_sbo_.address;
      // pc.splat_addr             = splat_sbo_.address;
      // pc.keys_addr              = splat_keys_sbo_.address;
      // pc.values_addr            = splat_values_sbo_.address;
      // pc.tile_ranges_addr       = tile_ranges_sbo_.address;
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
      pipeline_layout_,
      uniform_buffer_,
      descriptor_set_layout_
    );
  }

  void runMarchingCubePipeline(CommandEncoder const& cmd) {

  }

  void update(float const dt) final {
    // Camera Uniform Data.
    host_data_.viewMatrix = camera_.view();
    context_.writeBuffer(uniform_buffer_, host_data_); //
  }

  void draw(CommandEncoder const& cmd) final {
    runMarchingCubePipeline(cmd);

    // auto pass = cmd.beginRendering();
    // cmd.endRendering();

    drawUI(cmd);
  }

 private:
  ArcBallController arcball_controller_{};

  shader_interop::UniformBufferData host_data_{};
  backend::Buffer uniform_buffer_{};

  // ----------

  // backend::Buffer gaussian_sbo_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  VkPipelineLayout pipeline_layout_{};
  shader_interop::PushConstant push_constant_{};
  std::array<Pipeline, Compute_kCount> compute_pipelines_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(MarchingCubeSample)

/* -------------------------------------------------------------------------- */
