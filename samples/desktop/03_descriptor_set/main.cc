/* -------------------------------------------------------------------------- */
//
//    03 - Hello Descriptor Set
//
//  Show a simple use of a descriptor set, with a 3D tetrahedron.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  static bool constexpr kFlipScreenVertically{ true };

  /* Shortcut to the uniform data type used. */
  using HostData_t = shader_interop::UniformData;

  struct Vertex_t {
    float Position[4];
    float Normal[3];
  };

  enum AttributeLocation {
    Position = 0,
    Normal   = 1,
    kAttributeLocationCount
  };

  std::vector<Vertex_t> const kVertices{
    {.Position = { 1.0f, 1.0f, 1.0f, 1.0f}, .Normal = { 0.577,  0.577,  0.577}},
    {.Position = {-1.0f,-1.0f, 1.0f, 1.0f}, .Normal = {-0.577, -0.577,  0.577}},
    {.Position = {-1.0f, 1.0f,-1.0f, 1.0f}, .Normal = {-0.577,  0.577, -0.577}},
    {.Position = { 1.0f,-1.0f,-1.0f, 1.0f}, .Normal = { 0.577, -0.577, -0.577}},
  };

  std::vector<uint16_t> const kIndices{
    0, 2, 1,  0, 1, 3,
    0, 3, 2,  1, 2, 3
  };

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->setTitle("03 - Πρίσμα");

    renderer_.set_clear_color({0.125f, 0.125f, 0.125f, 1.0f});

    /* Initialize the scene data on the host, here just the camera matrices. */
    host_data_.scene.camera = {
      .viewMatrix = linalg::lookat_matrix(
        vec3f(1.0f, 2.0f, 5.0f),
        vec3f(0.0f, 0.0f, 0.0f),
        vec3f(0.0f, 1.0f, 0.0f)
      ),
      .projectionMatrix = linalg::perspective_matrix(
        lina::radians(60.0f),
        static_cast<float>(viewport_size_.width) / static_cast<float>(viewport_size_.height),
        0.01f,
        500.0f,
        linalg::neg_z,
        linalg::zero_to_one
      ),
    };

    /* Create & upload Uniform, Vertex & Index buffers on the device. */
    {
      auto cmd = context_.create_transient_command_encoder();

      uniform_buffer_ = cmd.create_buffer_and_upload(
        &host_data_, sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      );

      vertex_buffer_ = cmd.create_buffer_and_upload(
        kVertices,
        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
      );

      index_buffer_ = cmd.create_buffer_and_upload(
        kIndices,
        VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
      );

      context_.finish_transient_command_encoder(cmd);
    }

    /* Create the descriptor set with its binding. */
    {
      uint32_t const kDescSetUniformBinding = 0u;

      descriptor_set_layout_ = context_.create_descriptor_set_layout({
        {
          .binding = kDescSetUniformBinding,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                        | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                        | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                        ,
        },
      });

      /**
       * Create a descriptor set from a layout and update it directly (optionnal).
       * The descriptor set can be update later on by calling 'update_descriptor_set'.
       **/
      descriptor_set_ = context_.create_descriptor_set(descriptor_set_layout_, {
        {
          .binding = kDescSetUniformBinding,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = { { uniform_buffer_.buffer } }
        }
      });
    }

    auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
      "simple.vert.glsl",
      "simple.frag.glsl",
    })};

    /* Setup the graphics pipeline. */
    {
      /* Here we create the the pipeline layout externally. */
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
                    .location = AttributeLocation::Normal,
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .offset = offsetof(Vertex_t, Normal),
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
            /** Here we use a mesh layout as triangle strip. */
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
          }
        }
      );
    }

    context_.release_shader_modules(shaders);

    return true;
  }

  void release() final {
    context_.destroy_descriptor_set_layout(descriptor_set_layout_);

    /* As we've created the pipeline layout externally we should destroy it manually.
     * We could also use 'graphics_pipeline_.layout()'' if we didn't kept it. */
    context_.destroy_pipeline_layout(pipeline_layout_);
    context_.destroy_pipeline(graphics_pipeline_);

    context_.destroy_buffer(index_buffer_);
    context_.destroy_buffer(vertex_buffer_);
    context_.destroy_buffer(uniform_buffer_);
  }

  void update(float const dt) final {
    /* Update the model world matrix. */
    float const tick{ frame_time() };
    push_constant_.model.worldMatrix = lina::rotation_matrix_axis(
      vec3f(0.2f * cosf(3.0f * tick), 0.8f, sinf(tick)),
      tick * 0.75f
    );
  }

  void draw() final {
    auto cmd = renderer_.begin_frame();
    {
      auto pass = cmd.begin_rendering();
      {
        pass.set_viewport_scissor(viewport_size_, kFlipScreenVertically);

        pass.bind_pipeline(graphics_pipeline_);
        pass.push_constant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);

        /**
         * We need to bind the descriptor set(s) used by each pipeline layout in
         * used.
         *
         * Like push_constant, if a pipeline is currently bound, the RenderPassEncoder
         * will automaticaly use their's as the targeted pipeline layout.
         **/
        pass.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);

        pass.bind_vertex_buffer(vertex_buffer_);

        /**
         * The 'bind_index_buffer' function specifies the buffer from which indices
         * are retrieved during 'draw_indexed' operations. By default, it expects
         * an index buffer with 32-bit unsigned integers (uint32).
         *
         * The second parameter allows you to specify a different index type,
         * such as VK_INDEX_TYPE_UINT16, for compatibility with smaller index formats.
         */
        pass.bind_index_buffer(index_buffer_, VK_INDEX_TYPE_UINT16);
        pass.draw_indexed(kIndices.size());
      }
      cmd.end_rendering();
    }
    renderer_.end_frame();
  }

 private:
  HostData_t host_data_{};
  backend::Buffer uniform_buffer_{};

  backend::Buffer vertex_buffer_{};
  backend::Buffer index_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};

  VkPipelineLayout pipeline_layout_{};
  Pipeline graphics_pipeline_{};
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
