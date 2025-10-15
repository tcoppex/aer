/* -------------------------------------------------------------------------- */
//
//    02 - Aloha XR
//
//   Show a simple XR demo, the verbose way.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

namespace shader_interop {
#include "../shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  struct Vertex_t {
    vec4 Position;
    vec4 Color;
  };

  enum AttributeLocation : uint8_t {
    Position = 0,
    Color    = 1,
  };

  std::vector<Vertex_t> const kVertices{
    {.Position = { -1.0f, -1.0f, +0.0f, 1.0f}, .Color = {0.25f, 0.12f, 1.0f, 1.0f}},
    {.Position = { +1.0f, -1.0f, +0.0f, 1.0f}, .Color = {1.0f, 0.25f, 0.12f, 1.0f}},
    {.Position = {  0.0f, +1.0f, +0.0f, 1.0f}, .Color = {0.12f, 1.0f, 0.25f, 1.0f}},
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.sample_count = VK_SAMPLE_COUNT_4_BIT;
    return S;
  }

  bool setup() final {
    renderer_.set_clear_color(vec4(0.125f, 0.125f, 0.125f, 1.0f));

    vertex_buffer_ = context_.transient_create_buffer(
      kVertices,
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
    );

    uniform_buffer_ = context_.create_buffer(
      2u * sizeof(shader_interop::UniformCameraData),
      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
      VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    /* Create the descriptor set with its binding. */
    {
      uint32_t const kDescSetUniformBinding = 0u;

      descriptor_set_layout_ = context_.create_descriptor_set_layout({{
        .binding = kDescSetUniformBinding,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 2u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                      | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                      ,
      }});

      descriptor_set_ = context_.create_descriptor_set(descriptor_set_layout_, {{
        .binding = kDescSetUniformBinding,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffers = { { uniform_buffer_.buffer } }
      }});
    }

    auto const shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
      "simple.vert",
      "simple.frag",
    })};

    pipeline_layout_ = context_.create_pipeline_layout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });

    graphics_pipeline_ = renderer_.create_graphics_pipeline(
      pipeline_layout_,
      {
        .vertex = {
          .module = shaders[0u].module,
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
          .module = shaders[1u].module,
          .targets = {
            {
              .format = renderer_.color_format(),
              .writeMask = VK_COLOR_COMPONENT_R_BIT
                         | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT
                         | VK_COLOR_COMPONENT_A_BIT
                         ,
            }
          },
        },
        .depthStencil = {
          .format = renderer_.depth_stencil_format(),
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_TRUE,
          .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        },
        .primitive = {
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        }
      }
    );

    context_.release_shader_modules(shaders);

    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      pipeline_layout_,
      graphics_pipeline_
    );
    context_.destroy_buffer(uniform_buffer_);
    context_.destroy_buffer(vertex_buffer_);
  }

  void update(float dt) final {
    if (!xr_) {
      return;
    }

    auto const& frame = xr_->frameData();

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
    context_.write_buffer(uniform_buffer_, camera_data);

    // Handle controllers inputs.
    float z_angle_delta{};
    float z_depth_delta{};
    {
      auto const& input = xr_->frameControlState();
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
      auto translateMatrix = linalg::translation_matrix(vec3(0.0f, 0.0f, tZ));

      auto rotationMatrix = lina::rotation_matrix_z(
        lina::radians(z_angle_delta * 180.0f)
      );

      push_constant_.modelMatrix = linalg::mul(
        push_constant_.modelMatrix,
        linalg::mul(translateMatrix, rotationMatrix)
      );
    }
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.begin_rendering();
    {
      pass.bind_pipeline(graphics_pipeline_);

      pass.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
      pass.push_constant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
      pass.bind_vertex_buffer(vertex_buffer_);
      pass.draw(kVertices.size());
    }
    cmd.end_rendering();
  }

 private:
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

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
