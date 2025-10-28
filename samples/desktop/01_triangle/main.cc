/* -------------------------------------------------------------------------- */
//
//    01 - Hello Triangle
//
//    Demonstrates how to render a triangle, using:
//        - Graphics Pipeline,
//        - Vertex buffer,
//        - Transient command buffer,
//        - RenderPassEncoder commands:
//            * Dynamic Viewport / Scissor states,
//            * bindPipeline / bindVertexBuffer / draw.
//
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
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
    {.Position = { -0.5f, -0.5f, 0.0f, 1.0f}, .Color = {0.5f, 0.2f, 1.0f, 1.0f}},
    {.Position = { +0.5f, -0.5f, 0.0f, 1.0f}, .Color = {1.0f, 0.5f, 0.2f, 1.0f}},
    {.Position = {  0.0f, +0.5f, 0.0f, 1.0f}, .Color = {0.2f, 1.0f, 0.5f, 1.0f}},
  };

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->set_title("01 - さんかくのセレナーデ");

    /* Setters / Getters are written in snake_case, commands in camelCase. */
    renderer_.set_clear_color({0.25f, 0.25f, 0.25f, 1.0f});

    /* Create a device storage buffer, then upload vertices host data to it.
     *
     * We use a (temporary) transient command buffer to create the device vertex
     * buffer.
     **/
    {
      auto cmd = context_.createTransientCommandEncoder();

      vertex_buffer_ = cmd.createBufferAndUpload(kVertices,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
      );

      /* Submit the command to the graphics queue. */
      context_.finishTransientCommandEncoder(cmd);
    }

    /* Load the precompiled shader modules (the '.spv' prefix is omitted). */
    auto const shaders = context_.createShaderModules(COMPILED_SHADERS_DIR, {
      "simple.vert.glsl",
      "simple.frag.glsl",
    });

    /* Setup the graphics pipeline.
     *
     * When no pipeline layout is specified, a default one is set internally
     * and will be destroy alongside the pipeline.
     * If one is provided the destruction is let to the user.
     **/
    graphics_pipeline_ = context_.createGraphicsPipeline({
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
      }
    });

    /* Release the shader modules. */
    context_.releaseShaderModules(shaders);

    return true;
  }

  void release() final {
    context_.destroyPipeline(graphics_pipeline_);
    context_.destroyBuffer(vertex_buffer_);
  }

  void draw(CommandEncoder const& cmd) final {
    /**
     * 'beginRendering' (dynamic_rendering) or 'beginRenderPass' (legacy rendering)
     * returns a RenderPassEncoder, which is a specialized CommandEncoder to specify
     * rendering operations to a specific output (here the swapchain directly).
     **/
    auto pass = cmd.beginRendering();
    {
      /**
       * Set the viewport and scissor.
       * Use the flag 'flip_y' to false to flip the Y-axis downward as per the
       * default Vulkan specs. It is set to true by default.
       *
       * As GraphicPipeline uses dynamic Viewport and Scissor states by default,
       * we need to specify them when using a graphic pipeline.
       *
       * As dynamic states are not bound to a pipeline they can be set whenever
       * during rendering before the draw command.
       **/
      pass.setViewportScissor(viewport_size_, false);

      pass.bindPipeline(graphics_pipeline_);
      pass.bindVertexBuffer(vertex_buffer_);
      pass.draw(kVertices.size());
    }
    cmd.endRendering();
  }

 private:
  backend::Buffer vertex_buffer_;
  Pipeline graphics_pipeline_;
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
