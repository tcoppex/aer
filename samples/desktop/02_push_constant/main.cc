/* -------------------------------------------------------------------------- */
//
//    02 - Hello Push Constants
//
//  Demonstrates how to uses PushConstant for quick update of uniform values.
//    - Add push constant range to pipeline layout.
//    - Push constant before binding a pipeline.
//    - Push constant after binding a pipeline.
//    - Set viewport-scissor with/without an offset.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

/* Constants & data structures shared between the host and the device. */
namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  static bool constexpr kFlipScreenVertically{ false };

  struct Vertex_t {
    float Position[4];
    float Color[4];
  };

  enum AttributeLocation {
    Position = 0,
    Color    = 1,
    kAttributeLocationCount
  };

  std::vector<Vertex_t> const kVertices{
    {.Position = { -0.7f, -0.4f, 0.0f, 1.0f}, .Color = {0.25f, 0.1f, 1.0f, 1.0f}},
    {.Position = {  0.7f, -0.4f, 0.0f, 1.0f}, .Color = {1.0f, 0.25f, 0.1f, 1.0f}},
    {.Position = {  0.0f,  0.8f, 0.0f, 1.0f}, .Color = {0.1f, 1.0f, 0.25f, 1.0f}},
  };

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->setTitle("02 - танцующий треугольник");

    renderer_.set_clear_color({0.60f, 0.65f, 0.55f, 1.0f});

    /* Setup the device vertex buffer. */
    {
      auto cmd = context_.create_transient_command_encoder();

      vertex_buffer_ = cmd.create_buffer_and_upload(
        kVertices,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      );

      context_.finish_transient_command_encoder(cmd);
    }

    auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
      "simple.vert.glsl",
      "simple.frag.glsl",
    })};

    /* Setup the graphics pipeline. */
    {
      /**
       * When two parameters are specified to 'create_graphics_pipeline' the first
       * one is either the layout descriptor, in that case the layout is destroyed internally
       * alongside the pipeline when destroy_pipeline is called, or the VkDesciptorLayout
       * object, in which case it must be destroyed by the user.
       * */
      PipelineLayoutDescriptor_t const layout_desc{
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS,
            .size = sizeof(shader_interop::PushConstant),
          }
        },
      };

      graphics_pipeline_ = context_.create_graphics_pipeline(
        layout_desc,
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
                .format = context_.default_color_format(),
                .writeMask = VK_COLOR_COMPONENT_R_BIT
                           | VK_COLOR_COMPONENT_G_BIT
                           | VK_COLOR_COMPONENT_B_BIT
                           | VK_COLOR_COMPONENT_A_BIT
                           ,
              }
            },
          },
          .depthStencil = {
            .format = context_.default_depth_stencil_format(),
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
          },
          .primitive = {
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            /* By default the cull mode is set to 'none' and front face are ordered counter clockwise,
             * so if we're not flipping the screen, triangles will not be displayed.
             * Uncomment to see the result.  */
            // .cullMode = VK_CULL_MODE_BACK_BIT,
            // .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
          }
        }
      );
    }
    context_.release_shader_modules(shaders);

    return true;
  }

  void release() final {
    context_.destroy_pipeline(graphics_pipeline_);
    context_.destroy_buffer(vertex_buffer_);
  }

  void draw(CommandEncoder const& cmd) final {
    /* Retrieve the current frame time. */
    float const tick{ frame_time() };

    /* Prepare values for left/right viewport-scissor, showing the two ways to do it. */
    VkExtent2D const half_screen{
      viewport_size_.width/2u,
      viewport_size_.height
    };
    VkRect2D const right_side{
      .offset = {.x = static_cast<int>(half_screen.width)},
      .extent = half_screen,
    };

    /* By specifying the pipeline layout to target, we can push constants
     * before the actual pipeline is bound. */
    cmd.push_constant(tick * 1.4f, graphics_pipeline_.layout());

    auto pass = cmd.begin_rendering();
    {
      pass.bind_pipeline(graphics_pipeline_);
      pass.bind_vertex_buffer(vertex_buffer_);

      // Left-side.
      {
        /* Set viewport-scissor using a VkExtent2D (with no offset). */
        pass.set_viewport_scissor(half_screen, kFlipScreenVertically);
        pass.draw(kVertices.size());
      }

      // Right-side.
      {
        /* Set viewport-scissor using a VkRect2D (with offset). */
        pass.set_viewport_scissor(right_side, !kFlipScreenVertically);

        /**
         * If no pipeline layout is specified, the pass encoder will take the
         * one from the currently bound pipeline, when available.
         *
         * This is only possible using a non const RenderPassEncoder.
         **/
        pass.push_constant(tick * 4.0f);
        pass.draw(kVertices.size());
      }
    }
    cmd.end_rendering();
  }

 private:
  backend::Buffer vertex_buffer_{};
  Pipeline graphics_pipeline_{};
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
