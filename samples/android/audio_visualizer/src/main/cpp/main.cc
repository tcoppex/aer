/* -------------------------------------------------------------------------- */
//
//    Audio Vizualizer
//
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

namespace shader_interop {
#include "../shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class AudioVisualizerSample final : public Application {
 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    return S;
  }

  bool setup() final {
    renderer_.set_clear_color(vec4(0.85f, 0.55f, 0.65f, 0.25f));
    renderer_.set_foveated_rendering_enabled(false);

    // --------------------------

    /* Create buffers. */
    {
      uniform_buffer_ = context_.createBuffer(
        2u * sizeof(shader_interop::UniformCameraData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Create the descriptor set with its binding. */
    {
      uint32_t const kDescSetUniformBinding = 0u;

      descriptor_set_layout_ = context_.createDescriptorSetLayout(
        {
          {
            .binding = kDescSetUniformBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .bindingFlags =
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
              | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
              | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
          }
        },
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
      );

      descriptor_set_ = context_.createDescriptorSet(
        descriptor_set_layout_,
        {{
          .binding = kDescSetUniformBinding,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = {{ uniform_buffer_.buffer }}
        }}
      );
    }

    auto const shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "main.slang");

    pipeline_layout_ = context_.createPipelineLayout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });
    graphics_pipeline_ = context_.createGraphicsPipeline(
      pipeline_layout_,
      {
        .vertex = {
          .module = shader.handle,
          .entryPoint = "vertexMain",
        },
        .fragment = {
          .module = shader.handle,
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
        }
      }
    );

    context_.releaseShaderModule(shader);

    // ----------------------------

    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      pipeline_layout_,
      graphics_pipeline_,
      uniform_buffer_
    );
  }

  void update(float dt) final {
    auto const& frame = xr_->frame_data();
    // auto const& input = xr_->frame_control_state();

    // Update uniform buffer for both eyes.
    std::vector<shader_interop::UniformCameraData> camera_data{
      {
        .projectionMatrix = frame.projMatrices[0],
        .viewMatrix = frame.viewMatrices[0]
      },
      {
        .projectionMatrix = frame.projMatrices[1],
        .viewMatrix = frame.viewMatrices[1]
      },
    };
    context_.writeBuffer(uniform_buffer_, camera_data);
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      auto pass = cmd.beginRendering();
      {
        pass.bindPipeline(graphics_pipeline_);
        pass.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
        pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
        pass.draw(3);
      }
      cmd.endRendering();
    }
    cmd.endRendering();
  }

 private:
  backend::Buffer uniform_buffer_{};
  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};

  VkPipelineLayout pipeline_layout_{};
  Pipeline graphics_pipeline_{};
  shader_interop::PushConstant push_constant_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(AudioVisualizerSample)

/* -------------------------------------------------------------------------- */
