/* -------------------------------------------------------------------------- */
//
//    04 - Hello Texture
//
//    Where we put some interpolated crabs on.
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

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->setTitle("04 - خوراي ، كىشىلەر ماڭا دىققەت قىلىۋاتىدۇ");

    renderer_.set_clear_color({ 0.94f, 0.93f, 0.94f, 1.0f});

    allocator_ptr_ = context_.allocator_ptr();

    /* Initialize the scene data. */
    host_data_.scene.camera = {
      .viewMatrix = linalg::lookat_matrix(
        vec3f(1.0f, 2.0f, 3.0f),
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

    /* Create a cube mesh procedurally on the host, with a default size value. */
    Geometry::MakeCube(cube_);

    /* Map to bind vertex attributes with their shader input location. */
    cube_.initialize_submesh_descriptors({
      { Geometry::AttributeType::Position, shader_interop::kAttribLocation_Position },
      { Geometry::AttributeType::Texcoord, shader_interop::kAttribLocation_Texcoord },
      { Geometry::AttributeType::Normal, shader_interop::kAttribLocation_Normal },
    });

    /* Create Buffers & Image(s). */
    {
      auto cmd = context_.create_transient_command_encoder();

      uniform_buffer_ = cmd.create_buffer_and_upload(
        &host_data_, sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      );

      /* Transfer the cube geometry (vertices attributes & indices) to the device. */
      vertex_buffer_ = cmd.create_buffer_and_upload(
        cube_.get_vertices(),
        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
      );
      index_buffer_ = cmd.create_buffer_and_upload(
        cube_.get_indices(),
        VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
      );

      /* Load a texture using the current transient command encoder. */
      if (std::string fn{ASSETS_DIR "textures/whynot.png"}; !context_.load_image_2d(cmd, fn, image_)) {
        LOGW("The texture image '{}' could not be found.", fn);
      }

      context_.finish_transient_command_encoder(cmd);
    }

    /* Alternatively the texture could have been loaded directly using an
     * internal transient command encoder. */
    // context_.load_image_2d(path_to_texture, image_);

    /* We don't need to keep the host data so we can clear them. */
    cube_.clear_indices_and_vertices();

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
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_Sampler,
          .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        },
      });

      descriptor_set_ = context_.create_descriptor_set(descriptor_set_layout_, {
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = { { uniform_buffer_.buffer } }
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_Sampler,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .images = {
            {
              .sampler = context_.default_sampler(),
              .imageView = image_.view,
              .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }
          }
        }
      });
    }

    auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
      "simple.vert.glsl",
      "simple.frag.glsl",
    })};

    /* Setup the graphics pipeline. */
    {
      VkPipelineLayout const pipeline_layout = context_.create_pipeline_layout({
        .setLayouts = { descriptor_set_layout_ },
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(shader_interop::PushConstant),
          }
        },
      });

      graphics_pipeline_ = renderer_.create_graphics_pipeline(pipeline_layout, {
        .vertex = {
          .module = shaders[0u].module,
          /* Get buffer descriptors compatible with the mesh vertex inputs.
           *
           * Most Geometry::MakeX functions used the same interleaved layout,
           * so they can be used interchangeably on the same static pipeline.*/
          .buffers = cube_.pipeline_vertex_buffer_descriptors(),
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
          .topology = cube_.vk_primitive_topology(),
          .cullMode = VK_CULL_MODE_BACK_BIT,
        }
      });
    }

    context_.release_shader_modules(shaders);

    return true;
  }

  void release() final {
    context_.destroy_descriptor_set_layout(descriptor_set_layout_);
    context_.destroy_pipeline_layout(graphics_pipeline_.layout());
    context_.destroy_pipeline(graphics_pipeline_);

    allocator_ptr_->destroy_image(&image_);

    allocator_ptr_->destroy_buffer(index_buffer_);
    allocator_ptr_->destroy_buffer(vertex_buffer_);
    allocator_ptr_->destroy_buffer(uniform_buffer_);
  }

  void update(float const dt) final {
    /* Update the world matrix. */
    float const tick{ frame_time() };

    push_constant_.model.worldMatrix = lina::rotation_matrix_axis(
      vec3(3.0f * tick, 0.8f, sinf(tick)),
      tick * 0.62f
    );
  }

  void draw() final {
    auto cmd = renderer_.begin_frame();
    {
      auto pass = cmd.begin_rendering();
      {
        pass.set_viewport_scissor(viewport_size_);

        pass.bind_pipeline(graphics_pipeline_);
        {
          pass.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
          pass.push_constant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);

          pass.bind_vertex_buffer(vertex_buffer_);
          pass.bind_index_buffer(index_buffer_, cube_.vk_index_type());
          pass.draw_indexed(cube_.get_index_count());

          // pass.draw(cube_.get_draw_descriptor(), vertex_buffer_, index_buffer_);
        }
      }
      cmd.end_rendering();
    }
    renderer_.end_frame();
  }

 private:
  ResourceAllocator* allocator_ptr_{};

  HostData_t host_data_{};
  backend::Buffer uniform_buffer_{};

  scene::Mesh cube_{};
  backend::Buffer vertex_buffer_{};
  backend::Buffer index_buffer_{};
  backend::Image image_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};

  Pipeline graphics_pipeline_{};
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
