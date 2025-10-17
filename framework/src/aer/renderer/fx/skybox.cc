/* -------------------------------------------------------------------------- */


#include "aer/core/camera.h"
#include "aer/renderer/render_context.h"
#include "aer/renderer/fx/skybox.h"
#include "aer/renderer/fx/postprocess/compute/compute_fx.h" //
#include "aer/renderer/fx/postprocess/post_fx_pipeline.h"

/* -------------------------------------------------------------------------- */

void Skybox::init(RenderContext& context) {
  LOGD("- Initialize Skybox.");

  auto& sampler_pool = context.sampler_pool();
  context_ptr_ = &context;

  envmap_.init(context);

  /* Precalculate the BRDF LUT. */
  computeSpecularBRDFLookup();

  /* Internal sampler */
  sampler_LinearClampMipMap_ = sampler_pool.get({
    .magFilter = VK_FILTER_LINEAR,
    .minFilter = VK_FILTER_LINEAR,
    .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .anisotropyEnable = VK_FALSE,
    .maxLod = VK_LOD_CLAMP_NONE,
  });

  /* Create the skybox geometry on the device. */
  {
    Geometry::MakeCube(cube_);

    cube_.initializeSubmeshDescriptors({
      { Geometry::AttributeType::Position, shader_interop::skybox::kAttribLocation_Position },
    });

    auto cmd = context.createTransientCommandEncoder();

    vertex_buffer_ = cmd.createBufferAndUpload(
      cube_.vertices(),
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
    );
    index_buffer_ = cmd.createBufferAndUpload(
      cube_.indices(),
      VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
    );

    context.finishTransientCommandEncoder(cmd);
    cube_.clear_indices_and_vertices();
  }

  /* Descriptor sets */
  {
    descriptor_set_layout_ = context.createDescriptorSetLayout({
      {
        .binding = shader_interop::skybox::kDescriptorSetBinding_Skybox_Sampler,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      },
    });

    descriptor_set_ = context.createDescriptorSet(descriptor_set_layout_, {
      {
        .binding = shader_interop::skybox::kDescriptorSetBinding_Skybox_Sampler,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = {
          {
            .sampler = sampler_LinearClampMipMap_, //
            .imageView = envmap_.get_image(Envmap::ImageType::Diffuse).view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          }
        }
      }
    });
  }

  pipeline_layout_ = context.createPipelineLayout({
    .setLayouts = { descriptor_set_layout_ },
    .pushConstantRanges = {
      {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    ,
        .size = sizeof(PushConstant_t),
      }
    },
  });

  /* Create the render pipeline */
  {
    auto shaders{context.createShaderModules(FRAMEWORK_COMPILED_SHADERS_DIR "skybox/", {
      "skybox.vert.glsl",
      "skybox.frag.glsl",
    })};

    /* Setup the graphics pipeline. */
    graphics_pipeline_ = context.createGraphicsPipeline(pipeline_layout_, {
      .vertex = {
        .module = shaders[0u].module,
        .buffers = cube_.pipeline_vertex_buffer_descriptors(),
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
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      },
      .primitive = {
        .topology = cube_.vk_primitive_topology(),
        .cullMode = VK_CULL_MODE_NONE,
      }
    });

    context.releaseShaderModules(shaders);
  }
}

// ----------------------------------------------------------------------------

void Skybox::release(RenderContext const& context) {
  context.destroyResources(
    specular_brdf_lut_,
    graphics_pipeline_,
    pipeline_layout_,
    descriptor_set_layout_,
    index_buffer_,
    vertex_buffer_
  );
  envmap_.release();
}

// ----------------------------------------------------------------------------

bool Skybox::setup(std::string_view hdr_filename) {
  setuped_ = envmap_.setup(hdr_filename);
  if (setuped_) {
    context_ptr_->descriptor_set_registry().update_scene_ibl(*this);
  }
  return setuped_;
}

// ----------------------------------------------------------------------------

void Skybox::render(RenderPassEncoder & pass, Camera const& camera) const {
  if (!is_valid()) {
    LOGW("Trying to render a non setup skybox.");
    return;
  }

  PushConstant_t push_constant{};

  std::array<mat4f, 2u> views{
    camera.view(0u),
    camera.view(1u)
  };
  views[0][3] = vec4(vec3(0.0f), views[0][3].w);
  views[1][3] = vec4(vec3(0.0f), views[1][3].w);

  auto const& world_matrix = context_ptr_->default_world_matrix();
  push_constant.mvpMatrix[0] = linalg::mul(
    linalg::mul(camera.proj(0), views[0]), world_matrix
  );
  push_constant.mvpMatrix[1] = linalg::mul(
    linalg::mul(camera.proj(1), views[1]), world_matrix
  );
  push_constant.hdrIntensity = 1.0f;

  pass.bindPipeline(graphics_pipeline_);
  {
    pass.bindDescriptorSet(
      descriptor_set_,
        VK_SHADER_STAGE_VERTEX_BIT
      | VK_SHADER_STAGE_FRAGMENT_BIT
    );

    pass.pushConstant(
      push_constant,
        VK_SHADER_STAGE_VERTEX_BIT
      | VK_SHADER_STAGE_FRAGMENT_BIT
    );

    pass.bindVertexBuffer(vertex_buffer_);
    pass.bindIndexBuffer(index_buffer_, cube_.vk_index_type());
    pass.drawIndexed(cube_.index_count());
  }
}

// ----------------------------------------------------------------------------

void Skybox::computeSpecularBRDFLookup() {
  class IntegrateBRDF final : public ComputeFx {
    const uint32_t kNumSamples{ 1024u };

    PushConstant_t push_constant_{
      .numSamples = kNumSamples,
    };

    bool resize(VkExtent2D const dimension) final {
      if (!ComputeFx::resize(dimension)) {
        return false;
      }

      push_constant_.mapResolution = dimension.width;

      images_ = {
        context_ptr_->createImage2D(
          push_constant_.mapResolution,
          push_constant_.mapResolution,
          VK_FORMAT_R16G16_SFLOAT,
            VK_IMAGE_USAGE_SAMPLED_BIT
          | VK_IMAGE_USAGE_STORAGE_BIT,
          "Skybox::SpecularBRDF_lut"
        )
      };

      return true;
    }

    void setup(VkExtent2D const dimension) final {
      ComputeFx::setup(dimension);

      context_ptr_->updateDescriptorSet(descriptor_set_, {
        {
          .binding = kDefaultStorageImageBindingOutput,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .images = { {
            .imageView = images_[0].view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          } },
        }
      });
    }

    std::string shader_name() const final {
      return FRAMEWORK_COMPILED_SHADERS_DIR "skybox/integrate_brdf.comp.glsl";
    }

    std::vector<VkPushConstantRange> push_constant_ranges() const final {
      return {
        {
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .size = sizeof(push_constant_),
        }
      };
    }

    void pushConstant(GenericCommandEncoder const &cmd) const final {
      cmd.pushConstant(
        push_constant_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT
      );
    }

    void releaseImagesAndBuffers() final {
      // Left empty to prevent the output image to be destroyed.
    }
  };

  TPostFxPipeline<IntegrateBRDF> brdf_pipeline{};
  brdf_pipeline.init(*context_ptr_);
  brdf_pipeline.setup({ kBRDFLutResolution, kBRDFLutResolution });

  auto const& cmd = context_ptr_->createTransientCommandEncoder(Context::TargetQueue::Compute);
  {
    brdf_pipeline.execute(cmd);
  }
  context_ptr_->finishTransientCommandEncoder(cmd);

  specular_brdf_lut_ = brdf_pipeline.image_output();
  brdf_pipeline.release();

  // [TODO] Calculate mip-maps.
}

/* -------------------------------------------------------------------------- */