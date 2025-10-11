/* -------------------------------------------------------------------------- */
//
//    05 - Hello Stencil Op
//
//  Where we look into the abyss via a multi passes stencil effect.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/scene/mesh.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  using HostData_t = shader_interop::UniformData;

  static constexpr float kPortalSize = 2.35f;

  struct StaticMesh_t : scene::Mesh {
    backend::Buffer vertex;
    backend::Buffer index;

    void draw(RenderPassEncoder const& pass, uint32_t instance_count = 1u) const {
      pass.bind_vertex_buffer(vertex);
      pass.bind_index_buffer(index, vk_index_type());
      pass.draw_indexed(get_index_count(), instance_count);
    }
  };

  enum class PipelineID : uint32_t {
    StencilMask,
    StencilTest,
    DepthMask,
    Rendering,
    kCount
  };

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->setTitle("05 - přemýšlet s portály");

    renderer_.set_clear_color({ 0.108f, 0.108f, 0.108f, 1.0f});

    /* Initialize the scene data. */
    host_data_.scene.camera = {
      .viewMatrix = linalg::lookat_matrix(
        vec3f(0.0f, 0.0f, 4.0f),
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

    /* Create Uniform, Vertices, and Indices Buffers. */
    {
      auto cmd = context_.create_transient_command_encoder();

      uniform_buffer_ = cmd.create_buffer_and_upload(
        &host_data_, sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      );

      /* Map to bind vertex attributes with their shader input location. */
      Geometry::AttributeLocationMap const attrib_location_map{
        { Geometry::AttributeType::Position, shader_interop::kAttribLocation_Position },
        { Geometry::AttributeType::Texcoord, shader_interop::kAttribLocation_Texcoord },
        { Geometry::AttributeType::Normal, shader_interop::kAttribLocation_Normal },
      };

      // Plane
      {
        auto &mesh = plane_;
        Geometry::MakePlane(mesh, kPortalSize);

        /* Initialize the descriptors used to bind vertex inputs. */
        mesh.initialize_submesh_descriptors(attrib_location_map);

        mesh.vertex = cmd.create_buffer_and_upload(
          mesh.get_vertices(),
          VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
        );
        mesh.index = cmd.create_buffer_and_upload(
          mesh.get_indices(),
          VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
        );
        mesh.clear_indices_and_vertices();
      }

      // Torus
      {
        auto &mesh = torus_;

        float const r = 0.5f * kPortalSize;
        Geometry::MakeTorus2(mesh, r, r * lina::kSqrtTwo, 48u, 32u);

        mesh.initialize_submesh_descriptors(attrib_location_map);

        mesh.vertex = cmd.create_buffer_and_upload(
          mesh.get_vertices(),
          VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
        );
        mesh.index = cmd.create_buffer_and_upload(
          mesh.get_indices(),
          VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
        );
        mesh.clear_indices_and_vertices();
      }

      context_.finish_transient_command_encoder(cmd);
    }

    /* Descriptor set. */
    {
      descriptor_set_layout_ = context_.create_descriptor_set_layout({
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                        | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                        | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                        ,
        },
      });

      descriptor_set_ = context_.create_descriptor_set(descriptor_set_layout_, {
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = { { uniform_buffer_.buffer } }
        },
      });
    }

    /* Create a Pipeline Layout to be shared. */
    pipeline_layout_ = context_.create_pipeline_layout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });

    auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
      "simple.vert.glsl",
      "instanced.vert.glsl",
      "simple.frag.glsl",
    })};

    /* Setup the pipelines. */
    {
      /* Pipeline descriptor "shared" between the stencil and the depth mask pipelines. */
      GraphicsPipelineDescriptor_t mask_pipeline_descriptor{
        .vertex = {
          .module = shaders[0u].module,
          .buffers = plane_.pipeline_vertex_buffer_descriptors(),
        },
        .fragment = {
          .module = shaders[2u].module,
          .targets = {
            {
              .format = renderer_.color_format(),
              .writeMask = 0u,
            }
          },
        },
        .depthStencil = {
          .format = renderer_.depth_stencil_format(),
          .depthTestEnable = VK_TRUE,
          .depthWriteEnable = VK_FALSE,
          .depthCompareOp = VK_COMPARE_OP_ALWAYS,
          .stencilTestEnable = VK_TRUE,
          .stencilFront = {
            .failOp = VK_STENCIL_OP_REPLACE,
            .passOp = VK_STENCIL_OP_REPLACE,
            .depthFailOp = VK_STENCIL_OP_REPLACE,
            .compareOp = VK_COMPARE_OP_ALWAYS,
            .compareMask = 0xff,
            .writeMask = 0xff,
            .reference = 1u,
          },
        },
        .primitive = {
          .topology = plane_.vk_primitive_topology(),
          .cullMode = VK_CULL_MODE_BACK_BIT,
        }
      };

      /* Render inside the stencil buffer. */
      pipelines_[PipelineID::StencilMask] = renderer_.create_graphics_pipeline(pipeline_layout_, mask_pipeline_descriptor);

      /* Render inside the portal using the stencil test and the instancing vertex shader. */
      pipelines_[PipelineID::StencilTest] = renderer_.create_graphics_pipeline(pipeline_layout_, {
        .vertex = {
          .module = shaders[1u].module,
          .buffers = torus_.pipeline_vertex_buffer_descriptors(),
        },
        .fragment = {
          .module = shaders[2u].module,
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
          .stencilTestEnable = VK_TRUE,
          .stencilFront = {
            .failOp = VK_STENCIL_OP_KEEP,
            .passOp = VK_STENCIL_OP_KEEP,
            .depthFailOp = VK_STENCIL_OP_KEEP,
            .compareOp = VK_COMPARE_OP_EQUAL,
            .compareMask = 0xFF,
            .writeMask = 0xFF,
            .reference = 1u,
          },
        },
        .primitive = {
          .topology = torus_.vk_primitive_topology(),
          .cullMode = VK_CULL_MODE_BACK_BIT,
        }
      });

      /* Render in the depth buffer, to mask the portal once we've rendered into it. */
      mask_pipeline_descriptor.depthStencil.depthWriteEnable = VK_TRUE;
      pipelines_[PipelineID::DepthMask] = renderer_.create_graphics_pipeline(pipeline_layout_, mask_pipeline_descriptor);

      /* Regular rendering pipeline, used outside the portal. Does not use the stencil buffer. */
      pipelines_[PipelineID::Rendering] = renderer_.create_graphics_pipeline(pipeline_layout_, {
        .vertex = {
          .module = shaders[0u].module,
          .buffers = torus_.pipeline_vertex_buffer_descriptors(),
        },
        .fragment = {
          .module = shaders[2u].module,
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
          .topology = torus_.vk_primitive_topology(),
          .cullMode = VK_CULL_MODE_BACK_BIT,
        }
      });
    }

    context_.release_shader_modules(shaders);

    return true;
  }

  void release() final {
    for (auto const& pipeline : pipelines_) {
      context_.destroy_pipeline(pipeline);
    }
    context_.destroy_descriptor_set_layout(descriptor_set_layout_);
    context_.destroy_pipeline_layout(pipeline_layout_);

    auto allocator = context_.allocator();
    allocator.destroy_buffer(plane_.index);
    allocator.destroy_buffer(plane_.vertex);
    allocator.destroy_buffer(torus_.index);
    allocator.destroy_buffer(torus_.vertex);
    allocator.destroy_buffer(uniform_buffer_);
  }

  void draw() final {
    float const tick{ frame_time() };

    mat4 const portal_world_matrix = linalg::mul(
      lina::rotation_matrix_x(lina::kHalfPi),
      lina::rotation_matrix_axis(
        vec3(2.45f * cosf(0.5f * tick), 1.35f, -1.4 * sinf(0.35f * tick)),
        0.35f * tick
      )
    );

    auto cmd = renderer_.begin_frame();
    {
      /* As the pipeline shared the same layout, we can bind them just once directly. */
      cmd.bind_descriptor_set(descriptor_set_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT);

      /* All objects but the last one use the same world matrix. */
      push_constant_.model.worldMatrix = portal_world_matrix;
      cmd.push_constant(push_constant_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT);

      auto pass = cmd.begin_rendering();
      {
        pass.set_viewport_scissor(viewport_size_);

        // Draw the portal mask into the stencil buffer.
        pass.bind_pipeline(pipelines_[PipelineID::StencilMask]);
        plane_.draw(pass);

        // Draw instanced rings when passing the stencil test.
        pass.bind_pipeline(pipelines_[PipelineID::StencilTest]);
        torus_.draw(pass, 256u);

        // Draw the portal mask into the depth buffer.
        pass.bind_pipeline(pipelines_[PipelineID::DepthMask]);
        plane_.draw(pass);

        // Draw regular objects 'outside' the portal.
        pass.bind_pipeline(pipelines_[PipelineID::Rendering]);
        {
          // Portal frame.
          torus_.draw(pass);

          // Outer-ring.
          push_constant_.model.worldMatrix = linalg::mul(
            linalg::scaling_matrix(vec3(3.0)),
            lina::rotation_matrix_z(-0.32f * tick)
          );
          pass.push_constant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
          torus_.draw(pass);
        }
      }
      cmd.end_rendering();
    }
    renderer_.end_frame();
  }

 private:
  HostData_t host_data_{};
  backend::Buffer uniform_buffer_{};

  StaticMesh_t plane_;
  StaticMesh_t torus_;

  VkPipelineLayout pipeline_layout_{};
  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};

  EnumArray<Pipeline, PipelineID> pipelines_;
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
