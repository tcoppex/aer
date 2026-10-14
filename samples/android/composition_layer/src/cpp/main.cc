/* -------------------------------------------------------------------------- */
//
//    Composition Layer
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

namespace shader_interop {
#include "../shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class CompositionLayerSample final : public Application {
 private:
  static constexpr uint32_t kLayerRes{ 1024u };

  struct Vertex_t {
    vec4 Position;
    vec4 Color;
  };

  enum AttributeLocation : uint8_t {
    Position = 0,
    Color    = 1,
  };

  std::vector<Vertex_t> const kVertices{
    {.Position = {-1.0f, -1.0f, +0.0f, 1.0f}, .Color = {0.25f, 0.12f, 1.0f, 1.0f}},
    {.Position = {+1.0f, -1.0f, +0.0f, 1.0f}, .Color = {1.0f, 0.25f, 0.12f, 1.0f}},
    {.Position = { 0.0f, +1.0f, +0.0f, 1.0f}, .Color = {0.12f, 1.0f, 0.25f, 1.0f}},
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.color_format = VK_FORMAT_R8G8B8A8_SRGB;
    return S;
  }

  bool setup() final {
    renderer_.set_clear_color(vec4(0.5f, 0.25f, 0.25f, 0.5f));
    renderer_.set_foveated_rendering_enabled(false);

    // --------------------------
    /* Create a secondary swapchain for the UI panel. */
    ui_swapchain_ = xr_->createCompositionSwapchain(kLayerRes, kLayerRes);
    if (!ui_swapchain_) {
      LOGE("failed to create ui swapchain");
      return false;
    }
    ui_swapchain_ptr_ = ui_swapchain_.get();

    ui_frame_handler_ = {
      .swapchain_pptr = &ui_swapchain_ptr_,
      .frames = context_.createFrameResources(
        *ui_swapchain_,
        {
          .colors = {{
            .format = settings().renderer.color_format,
            .clear_value = {{ 0.25f, 0.85f, 0.5f, 0.95f }}
          }},
          .depth_stencil = {
            .format = context_.default_depth_stencil_format(),
          },
          .size = { kLayerRes, kLayerRes },
          .array_size = 1u,
          .sample_count = VK_SAMPLE_COUNT_1_BIT,
          .debug_prefix = "UI_CompositionLayer_RT",
        }
      ),
    };
    // --------------------------

    /* Create buffers. */
    {
      vertex_buffer_ = context_.transientCreateBuffer(
        kVertices,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      );

      uniform_buffer_ = context_.createBuffer(
        2u * sizeof(shader_interop::UniformCameraData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Create the descriptor set with its binding. */
    {
      uint32_t const kDescSetUniformBinding = 0u;

      descriptor_set_layout_ = context_.createDescriptorSetLayout({{
        .binding = kDescSetUniformBinding,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 2u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                      | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                      ,
      }});

      descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {{
        .binding = kDescSetUniformBinding,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffers = { { uniform_buffer_.buffer } }
      }});
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
          .buffers = {
            {
              .stride = sizeof(Vertex_t),
              .attributes =  {
                {
                  .location = AttributeLocation::Position,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = offsetof(Vertex_t, Position),
                },
                {
                  .location = AttributeLocation::Color,
                  .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                  .offset = offsetof(Vertex_t, Color),
                },
              }
            }
          }
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

    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      pipeline_layout_,
      graphics_pipeline_,
      uniform_buffer_,
      vertex_buffer_
    );
    context_.releaseFrameResources(ui_frame_handler_.frames);
    ui_swapchain_.reset();
  }

  void updateUILayer() {
    if (!ui_swapchain_) {
      LOGW("UI swapchain not setup!");
      return;
    }

    auto &cmd = renderer_.beginFrame(&ui_frame_handler_);

    draw(cmd);

    if (renderer_.endFrame()) {
      auto const& xr_control = xr_->control_state();

      // Create a floating quad composition layer positioned on top of the right controller
      auto grip_pose = xr_control.right_grip_pose();
      {
        auto orient = std::bit_cast<vec4>(grip_pose.orientation);
        orient = lina::qmul(orient, lina::rotation_quat(vec3(1, 0, 0), -1.57f));

        auto pos = std::bit_cast<vec3>(grip_pose.position);
        auto rotated_offset = lina::qrot(orient, vec3(0.0f, 0.125f, 0.0f));
        pos = pos + rotated_offset;

        grip_pose = {
          .orientation = std::bit_cast<XrQuaternionf>(orient),
          .position = std::bit_cast<XrVector3f>(pos)
        };
      }

      xr_->pushCompositionLayer({.quad = {
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = xr_->get_space(XRSpaceId::Head),
        .subImage = {
          .swapchain = ui_swapchain_->handle(),
          .imageRect = ui_swapchain_->rect(),
          .imageArrayIndex = 0
        },
        .pose = grip_pose,
        .size = {0.1f, 0.1f},
      }});
    }
  }

  void update(float dt) final {
    auto const& frame = xr_->frame_data();

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

    // ---------------------

    // Handle controllers inputs.
    float z_angle_delta{};
    float z_depth_delta{};
    {
      auto const& input = xr_->frame_control_state();

      if (input.button_thumbstick[XRSide::Right]) {
        renderer_.set_foveated_rendering_enabled(
          !renderer_.is_foveated_rendering_enabled()
        );
      }
      if (input.button_a || input.button_x) {
        space_id_ = static_cast<XRSpaceId>(
          (space_id_ == 0u) ? frame.spaceMatrices.size() - 1u : space_id_ - 1
        );
      }
      if (input.button_b || input.button_y) {
        space_id_ = static_cast<XRSpaceId>(
          (space_id_ >= frame.spaceMatrices.size()-1u) ? 0u : space_id_ + 1
        );
      }
      if (auto *ptr = frame.spaceMatrices[space_id_]; nullptr != ptr) {
        push_constant_.modelMatrix = *ptr;
      }

      z_angle_delta = lina::smoothstep(0.0f, 1.0f, input.index_trigger[XRSide::Right]);
      z_depth_delta = lina::smoothstep(0.0f, 1.0f, input.grip_squeeze[XRSide::Right]);
    }

    // Calculate new model matrix.
    {
      float const tZ = -10.0f + z_depth_delta * 7.0f;
      auto translateMatrix = lina::translation_matrix(vec3(0.0f, 0.0f, tZ));

      auto rotationMatrix = lina::rotation_matrix_z(
        lina::radians(z_angle_delta * 180.0f)
      );

      push_constant_.modelMatrix = lina::mul(
        push_constant_.modelMatrix,
        lina::mul(translateMatrix, rotationMatrix)
      );
    }

    // ---------------------

    updateUILayer();
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      pass.bindPipeline(graphics_pipeline_);
      pass.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
      pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
      pass.bindVertexBuffer(vertex_buffer_);
      pass.draw(kVertices.size());
    }
    cmd.endRendering();
  }

 private:
  // --------------
  std::shared_ptr<OpenXRSwapchain> ui_swapchain_{};
  SwapchainInterface *ui_swapchain_ptr_{};
  Renderer::FrameHandler ui_frame_handler_{};
  // --------------

  backend::Buffer vertex_buffer_{};
  backend::Buffer uniform_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};

  VkPipelineLayout pipeline_layout_{};
  Pipeline graphics_pipeline_{};

  shader_interop::PushConstant push_constant_{};
  XRSpaceId space_id_{XRSpaceId::Local};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(CompositionLayerSample)

/* -------------------------------------------------------------------------- */
