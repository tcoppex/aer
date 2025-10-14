/* -------------------------------------------------------------------------- */
//
//    08 - image based lighting
//
//  Where we use a HDR envmap to illuminate a 3D model.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

#include "aer/core/camera.h"
#include "aer/core/arcball_controller.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

static constexpr uint32_t kMaxNumTextures = 128u;

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  using HostData_t = shader_interop::UniformData;

 private:
  bool setup() final {
    wm_->setTitle("08 - Es werde Licht");

    renderer_.set_clear_color({ 0.55f, 0.65f, 0.75f, 1.0f });

    /* Setup the ArcBall camera. */
    {
      camera_.setPerspective(
        lina::radians(60.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.05f,
        750.0f
      );
      camera_.setController(&arcball_controller_);

      arcball_controller_.setView(lina::kTwoPi/16.0f, lina::kTwoPi/8.0f);
      arcball_controller_.setDolly(4.0f);

      host_data_.scene = {
        .projectionMatrix = camera_.proj(),
      };

      /* Create and upload the uniform values directly. */
      if constexpr (true) {
        /* Using an internal transient command buffer. */
        uniform_buffer_ = context_.transient_create_buffer(
          &host_data_, sizeof(host_data_),
          VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
        );
      } else {
        /* Or writing the data from the host via mapping operations. */
        uniform_buffer_ = context_.create_buffer(
          sizeof(host_data_),
          VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT,
          VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        context_.write_buffer(uniform_buffer_, &host_data_, sizeof(host_data_));
      }
    }

    /* Initialize the skybox and setup the HDR diffuse envmap used to calculate
     * IBL convolutions. */
    auto &skybox = renderer_.skybox();
    skybox.setup(ASSETS_DIR "textures/qwantani_dusk_2_2k.hdr");

    /* Load glTF Scene / Scene. */
    {
      std::string const gltf_filename{ASSETS_DIR "models/"
        "suzanne.glb"
      };

      /* Load the model directly on device, as we do not change the model's internal data
       * layout we need to specify how to map its attributes to the shader used. */
      scene_ = renderer_.load_gltf(gltf_filename, {
        { Geometry::AttributeType::Position,  shader_interop::kAttribLocation_Position },
        { Geometry::AttributeType::Texcoord,  shader_interop::kAttribLocation_Texcoord },
        { Geometry::AttributeType::Normal,    shader_interop::kAttribLocation_Normal   },
      });

      LOG_CHECK(scene_->device_images.size() <= kMaxNumTextures); //
    }

    /* Release the temporary staging buffers.
     * This is done automatically at the end of setup(). */
    context_.clear_staging_buffers();

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
          .descriptorCount = kMaxNumTextures,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_IrradianceEnvMap,
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
          .buffers = { { uniform_buffer_.buffer } },
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_IrradianceEnvMap,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .images = {
            {
              .sampler = context_.default_sampler(),
              .imageView = skybox.irradiance_map().view,
              .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }
          },
        },
      });
    }

    /* Update the Sampler Atlas descriptor with the currently loaded textures. */
    context_.update_descriptor_set(descriptor_set_, {
      {
        .binding = shader_interop::kDescriptorSetBinding_Sampler,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = scene_->descriptor_image_infos()
      }
    });

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
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                        | VK_SHADER_STAGE_FRAGMENT_BIT
                        ,
            .size = sizeof(shader_interop::PushConstant),
          }
        },
      });

      graphics_pipeline_ = renderer_.create_graphics_pipeline(pipeline_layout, {
        .dynamicStates = {
          VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
          VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        },
        .vertex = {
          .module = shaders[0u].module,
        },
        .fragment = {
          .module = shaders[1u].module,
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
    context_.destroy_buffer(uniform_buffer_);
    scene_.reset();
  }

  void draw_model(RenderPassEncoder const& pass, mat4 const& world_matrix) {
    for (auto const& mesh : scene_->meshes) {
      pass.set_primitive_topology(mesh->vk_primitive_topology());
      push_constant_.model.worldMatrix = linalg::mul(
        world_matrix,
        mesh->world_matrix()
      );
      for (auto const& submesh : mesh->submeshes) {
        auto material = scene_->material(*submesh.material_ref);
        push_constant_.model.albedo_texture_index = material.bindings.basecolor;
        pass.push_constant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT); //
        pass.draw(submesh.draw_descriptor, scene_->vertex_buffer, scene_->index_buffer);
      }
    }
  }

  void update(float const dt) final {
    camera_.update( dt );
    push_constant_.viewMatrix = camera_.view();
  }

  void draw() final {
    mat4 const worldMatrix{
      lina::rotation_matrix_axis(
        vec3(-0.25f, 1.0f, -0.15f),
        frame_time() * 0.4f
      )
    };

    auto cmd = renderer_.begin_frame();
    {
      auto pass = cmd.begin_rendering();
      {
        pass.set_viewport_scissor(viewport_size_);

        /* First render the skybox. */
        renderer_.skybox().render(pass, camera_);

        /* Then the scene / model. */
        pass.bind_pipeline(graphics_pipeline_);
        {
          pass.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
          draw_model(pass, worldMatrix);
        }
      }
      cmd.end_rendering();
    }
    renderer_.end_frame();
  }

 private:
  HostData_t host_data_{};
  backend::Buffer uniform_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};

  Pipeline graphics_pipeline_{};

  // -----------------------

  Camera camera_{};
  ArcBallController arcball_controller_{};

  GLTFScene scene_{};
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
