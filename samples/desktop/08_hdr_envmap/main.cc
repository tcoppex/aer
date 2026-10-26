/* -------------------------------------------------------------------------- */
//
//    08 - image based lighting
//
//  Where we use a HDR envmap to illuminate a 3D model.
//
//  Show how to load and display a glTF scene manually, redefining the pipeline.
//
//  For a simpler approach, sample #10 show how to use the internal renderer
//  system directly.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
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
    wm_->set_title("08 - Es werde Licht");

    renderer_.set_clear_color({ 0.55f, 0.65f, 0.75f, 1.0f });

    /* Setup the camera. */
    {
      /* We use the internal camera_ object, on which we give an ArcBallController
       * to be able to move it with input events. */
      camera_.makePerspective(
        lina::radians(60.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.05f,
        750.0f
      );
      camera_.set_controller(&arcball_controller_);

      arcball_controller_.set_view(lina::kTwoPi/16.0f, lina::kTwoPi/8.0f);
      arcball_controller_.set_dolly(4.0f);

      host_data_.scene = {
        .projectionMatrix = camera_.proj(),
      };

      /* Create and upload the uniform values directly. */
      if constexpr (true) {
        /* Using an internal transient command buffer. */
        uniform_buffer_ = context_.transientCreateBuffer(
          &host_data_, sizeof(host_data_),
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        );
      } else {
        /* Or writing the data from the host via mapping operations. */
        uniform_buffer_ = context_.createBuffer(
          sizeof(host_data_),
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VMA_MEMORY_USAGE_CPU_TO_GPU
        );
        context_.writeBuffer(uniform_buffer_, &host_data_, sizeof(host_data_));
      }
    }

    /* Initialize the skybox and setup the HDR diffuse envmap used to calculate
     * IBL convolutions. */
    renderer_.setSkybox(ASSETS_DIR "textures/qwantani_dusk_2_2k.hdr");

    /* Load glTF Scene / Scene. */
    {
      auto const gltf_filename = std::string{ASSETS_DIR "models/suzanne.glb"};

      /* Load the model asset directly on device memory (but not on the scene tree). */
      if (auto ah = renderer_.loadAsset(gltf_filename); ah.valid()) {
        asset_ = renderer_.gpu_pool().find(ah);
      } else {
        LOGE("Asset \"{}\" failed to load.", gltf_filename);
        return false;
      }

      if (!asset_) {
        LOGE("Asset \"{}\" not found.", gltf_filename);
        return false;
      }
    }

    /* Release the temporary staging buffers.
     * This is done automatically at the end of setup(). */
    context_.clearStagingBuffers();

    /* Descriptor set. */
    {
      descriptor_set_layout_ = context_.createDescriptorSetLayout({
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

      descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
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
              .imageView = renderer_.skybox().irradiance_map().view,
              .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            }
          },
        },
      });
    }

    // ---------------------------------
    /* Update the Sampler Atlas descriptor with the currently loaded textures. */
    {
      std::vector<VkDescriptorImageInfo> image_infos{};
      image_infos.reserve(asset_->texture_count);

      auto const& sampler_pool = context_.sampler_pool();
      auto const& images       = renderer_.gpu_pool().images();

      /* Here the asset has already been uploaded on device with its internal
       * sampler to its material fx pipeline. As we lost its internal resoruces,
       * to bind textures (already on memory) to our custom pipeline
       * we have to give it a custom sampler.
       */
      for (uint32_t i = 0; i < asset_->texture_count; ++i) {
        auto const& img = images.at(asset_->first_texture + i);
        image_infos.push_back({
          .sampler = sampler_pool.default_sampler(),
          .imageView = img.view,
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        });
      }

      context_.updateDescriptorSet(descriptor_set_, {
        {
          .binding = shader_interop::kDescriptorSetBinding_Sampler,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .images = std::move(image_infos)
        }
      });
    }
    // ---------------------------------


    /* Setup the graphics pipeline. */
    {
      VkPipelineLayout const pipeline_layout = context_.createPipelineLayout({
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

      auto shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "main.slang");

      graphics_pipeline_ = context_.createGraphicsPipeline(pipeline_layout, {
        .dynamicStates = {
          VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
          VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
        },
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
          .cullMode = VK_CULL_MODE_BACK_BIT,
        }
      });

      context_.releaseShaderModule(shader);
    }


    return true;
  }

  void release() final {
    context_.destroyResources(
      descriptor_set_layout_,
      graphics_pipeline_.layout(),
      graphics_pipeline_,
      uniform_buffer_
    );
  }

  void update(float const dt) final {
    /* We can check if the camera has changed between frame. */
    if (camera_.rebuilt()) {
      push_constant_.viewMatrix = camera_.view();
    }
  }

  void drawPass(RenderPassEncoder const& pass) final {
    if (!asset_) {
      return;
    }

    mat4 const world_matrix = lina::rotation_matrix_axis(
      vec3(-0.25f, 1.0f, -0.15f),
      frame_time() * 0.4f
    );

    pass.bindPipeline(graphics_pipeline_);
    pass.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);

    auto const& pool = renderer_.gpu_pool();

    for (auto const& mesh : asset_->meshes) {
      pass.setPrimitiveTopology(mesh.topology);

      auto pc = push_constant_;
      pc.model.worldMatrix = world_matrix;

      for (auto const& submesh : mesh.submeshes) {
        if (submesh.asset_material_index != kInvalidIndexU32) {
          // auto const& mat = scene_->material_proxy(*submesh.material_ref);
          // pc.model.albedo_texture_index = mat.bindings.basecolor;
        } else {
          LOGW("Submesh lack a material reference.");
        }
        pass.pushConstant(pc, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        pass.bindAndDraw(submesh.draw_descriptor, pool.vertex_buffer(), pool.index_buffer());
      }
    }

  }

 private:
  ArcBallController arcball_controller_{};

  DeviceAsset const* asset_{};

  HostData_t host_data_{};
  backend::Buffer uniform_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};
  Pipeline graphics_pipeline_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
