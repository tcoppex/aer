/* -------------------------------------------------------------------------- */
//
//    01 - Aloha Android
//
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

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
    {.Position = { -0.5f, -0.5f, 0.0f, 1.0f}, .Color = {0.5f, 0.2f, 1.0f, 1.0f}},
    {.Position = { +0.5f, -0.5f, 0.0f, 1.0f}, .Color = {1.0f, 0.5f, 0.2f, 1.0f}},
    {.Position = {  0.0f, +0.5f, 0.0f, 1.0f}, .Color = {0.2f, 1.0f, 0.5f, 1.0f}},
  };

  static
  std::array<vec4, 9u> constexpr kColors{
    vec4(0.89f, 0.45f, 0.00f, 1.0f),
    vec4(0.52f, 0.26f, 0.75f, 1.0f),
    vec4(0.48f, 0.60f, 0.61f, 1.0f),
    vec4(0.43f, 0.87f, 0.62f, 1.0f),
    vec4(0.98f, 0.98f, 0.98f, 1.0f),
    vec4(0.96f, 0.38f, 0.32f, 1.0f),
    vec4(0.24f, 0.16f, 0.09f, 1.0f),
    vec4(0.96f, 0.51f, 0.66f, 1.0f),
    vec4(0.70f, 0.85f, 0.45f, 1.0f),
  };

 public:
  SampleApp() = default;

 private:
  bool setup() final {
    renderer_.set_clear_color(vec4(0.75f, 0.15f, 0.30f, 1.0f));

    vertex_buffer_ = context_.create_buffer_and_upload(
      kVertices, VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
    );

    auto const shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
      "simple.vert",
      "simple.frag",
    })};

    graphics_pipeline_ = renderer_.create_graphics_pipeline({
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
    });

    context_.release_shader_modules(shaders);

    return true;
  }

  void release() final {
    context_.destroy_pipeline(graphics_pipeline_);
    context_.allocator().destroy_buffer(vertex_buffer_);
  }

  void draw() final {
    auto cmd = renderer_.begin_frame();

    auto pass = cmd.begin_rendering();
    {
      pass.set_viewport_scissor(viewport_size_, false);
      pass.bind_pipeline(graphics_pipeline_);
      pass.bind_vertex_buffer(vertex_buffer_);
      pass.draw(kVertices.size());
    }
    cmd.end_rendering();

    renderer_.end_frame();
  }

  void onPointerUp(int x, int y, KeyCode_t button) final {
    clear_color_index_ = (clear_color_index_ + 1u)  % kColors.size();
    renderer_.set_clear_color(kColors[clear_color_index_]);
  }

 private:
  backend::Buffer vertex_buffer_{};
  Pipeline graphics_pipeline_{};
  uint32_t clear_color_index_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
