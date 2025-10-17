/* -------------------------------------------------------------------------- */
//
//    09 - post processing
//
//  Where we stay in image space to improve our rendering.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/core/arcball_controller.h"
#include "aer/renderer/fx/postprocess/post_fx_pipeline.h"
#include "aer/renderer/fx/postprocess/compute/impl/depth_minmax.h"
#include "aer/renderer/fx/postprocess/fragment/impl/normaldepth_edge.h"
#include "aer/renderer/fx/postprocess/fragment/impl/object_edge.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

/**
 * Simple render pass for gltf objects, creating two texture outputs :
 *  - RGBA_32F Color Texture,
 *  - RGBA_32F Data Texture (XY Normal + Z Depth + W ObjectId).
**/
class SceneFx final : public RenderTargetFx {
 private:
  static constexpr uint32_t kMaxNumTextures = 128u;

 public:
  void setup(VkExtent2D const dimension) final {
    RenderTargetFx::setup(dimension);

    uniform_buffer_ = context_ptr_->createBuffer(
      sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );

    context_ptr_->updateDescriptorSet(descriptor_set_, {
      {
        .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .buffers = { { uniform_buffer_.buffer } },
      }
    });
  }

  void release() final {
    context_ptr_->destroyBuffer(uniform_buffer_);
    scene_.reset();
    RenderTargetFx::release();
  }

 protected:
  std::string vertex_shader_name() const final {
    return COMPILED_SHADERS_DIR "scene.vert.glsl";
  }

  std::string shader_name() const final {
    return COMPILED_SHADERS_DIR "scene.frag.glsl";
  }

  void createRenderTarget(VkExtent2D const dimension) final {
    render_target_ = context_ptr_->createRenderTarget({
      .colors = {
        {
          .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .clear_value = {{ 0.67f, 0.82f, 0.69f, 0.0f }}
        },
        {
          .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .clear_value = {{ 0.0f, 0.0f, -1.0f, 0.0f }}
        },
      },
      .depth_stencil = { VK_FORMAT_D24_UNORM_S8_UINT },
      .size = dimension,
      .sample_count = VK_SAMPLE_COUNT_1_BIT, //
    });
  }

  DescriptorSetLayoutParamsBuffer descriptor_set_layout_params() const final {
    return {
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
      }
    };
  }

  std::vector<VkPushConstantRange> push_constant_ranges() const final {
    return {
      {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    ,
        .size = sizeof(push_constant_),
      }
    };
  }

  void pushConstant(GenericCommandEncoder const &cmd) const final {
    cmd.pushConstant(push_constant_, pipeline_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
  }

  GraphicsPipelineDescriptor_t graphics_pipeline_descriptor(std::vector<backend::ShaderModule> const& shaders) const final {
    return {
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
          { .format = render_target_->resolve_attachment(0).format },
          { .format = render_target_->resolve_attachment(1).format }
        },
      },
      .depthStencil = {
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      },
      .primitive = {
        .cullMode = VK_CULL_MODE_BACK_BIT,
      },
      .multisample = {
        .sampleCount = render_target_->sample_count(),
      }
    };
  }

  void draw(RenderPassEncoder const& pass) const final {
    uint32_t instance_index = 0u;
    for (auto const& mesh : scene_->meshes) {
      pass.setPrimitiveTopology(mesh->vk_primitive_topology());

      push_constant_.model.worldMatrix = linalg::mul(
        world_matrix_,
        mesh->world_matrix()
      );

      for (auto const& submesh : mesh->submeshes) {
        auto material = scene_->material(*submesh.material_ref);
        push_constant_.model.albedo_texture_index = material.bindings.basecolor;
        push_constant_.model.material_index = submesh.material_ref->material_index;
        push_constant_.model.instance_index = instance_index++;
        pushConstant(pass);
        pass.draw(submesh.draw_descriptor, scene_->vertex_buffer, scene_->index_buffer);
      }
    }
  }

 public:
  void setModel(GLTFScene model) {
    LOG_CHECK(model->device_images.size() <= kMaxNumTextures); //

    scene_ = model;

    /* Update the Sampler Atlas descriptor with the currently loaded textures. */
    context_ptr_->updateDescriptorSet(descriptor_set_, {
      {
        .binding = shader_interop::kDescriptorSetBinding_Sampler,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = scene_->buildDescriptorImageInfos()
      }
    });
  }

  void setWorldMatrix(mat4 const& world_matrix) {
    world_matrix_ = world_matrix;
  }

  void setCameraPosition(vec3 const& camera_position) {
    push_constant_.cameraPosition = camera_position;
  }

  void setViewMatrix(mat4 const& view_matrix) {
    push_constant_.viewMatrix = view_matrix;
  }

  void setProjectionMatrix(mat4 const& projection_matrix) {
    host_data_.scene.projectionMatrix = projection_matrix;
  }

  void updateUniforms() {
    context_ptr_->transientUploadBuffer(
      &host_data_, sizeof(host_data_), uniform_buffer_
    );
  }

 private:
  mutable shader_interop::PushConstant push_constant_{};

  shader_interop::UniformData host_data_{};
  backend::Buffer uniform_buffer_{};

  GLTFScene scene_{};
  mat4 world_matrix_{};
};

// ----------------------------------------------------------------------------

/**
 * Customized PostFxPipeline which use a SceneFx as entry point, and a custom
 * FragmentFx as final compositions tage.
**/
class ToonFxPipeline final : public TPostFxPipeline<SceneFx> {
 public:
  class ToonComposition final : public RenderTargetFx {
    std::string shader_name() const final {
      return COMPILED_SHADERS_DIR "toon.frag.glsl";
    }
  };

 public:
  void init(RenderContext const& context) final {
    auto entry_fx = getEntryFx();

    auto depth_minmax = add<fx::compute::DepthMinMax>({
      .images = { {entry_fx, 1u} }
    });

    auto normaldepth_edge = add<fx::frag::NormalDepthEdge>({
      .images = { {entry_fx, 1u} },
      .buffers = { {depth_minmax, 0u} }
    });

    auto object_edge = add<fx::frag::ObjectEdge>({
      .images = { {entry_fx, 1u} },
    });

    auto toon = add<ToonComposition>({
      .images = {
        {entry_fx, 0u},
        {normaldepth_edge, 0u},
        {object_edge, 0u}
      },
    });

    TPostFxPipeline<SceneFx>::init(context);
  }
};

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->set_title("09 - una riga alla volta");

    /* Setup the ArcBall camera. */
    {
      camera_.makePerspective(
        lina::radians(45.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.01f,
        500.0f
      );
      camera_.set_controller(&arcball_controller_);

      arcball_controller_.set_view(lina::kTwoPi/16.0f, lina::kTwoPi/8.0f, false);
      arcball_controller_.set_dolly(4.0f, false);
    }

    /* Load a glTF Scene. */
    std::string const gltf_filename{ASSETS_DIR "models/"
      "DamagedHelmet.glb"
    };

    auto gltf_scene = renderer_.loadGLTF(gltf_filename, {
      { Geometry::AttributeType::Position,  shader_interop::kAttribLocation_Position },
      { Geometry::AttributeType::Texcoord,  shader_interop::kAttribLocation_Texcoord },
      { Geometry::AttributeType::Normal,    shader_interop::kAttribLocation_Normal   },
    });

    /* Fx Pipeline. */
    toon_pipeline_.init(context_);
    toon_pipeline_.setup(renderer_.surface_size()); //

    if (auto sceneFx = toon_pipeline_.getEntryFx(); sceneFx) {
      sceneFx->setModel(gltf_scene);
      sceneFx->setProjectionMatrix(camera_.proj());
      sceneFx->updateUniforms();
    }

    return true;
  }

  void release() final {
    toon_pipeline_.release();
  }

  void update(float const dt) final {
    mat4 const world_matrix{
      lina::rotation_matrix_axis(
        vec3(-0.25f, 1.0f, -0.15f),
        frame_time() * 0.075f //
      )
    };

    auto sceneFx = toon_pipeline_.getEntryFx();
    sceneFx->setCameraPosition(camera_.position());
    sceneFx->setViewMatrix(camera_.view());
    sceneFx->setWorldMatrix(world_matrix);
  }

  void draw(CommandEncoder const& cmd) final {
    /* Main rendering + Toon post-processing. */
    toon_pipeline_.execute(cmd);

    /* Blit the result directly to the current swapchain image. */
    renderer_.blitColor(cmd, toon_pipeline_.image_output());

    /* Draw UI on top. */
    drawUI(cmd);
  }

  void buildUI() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Separator();

      if (ImGui::CollapsingHeader("Post-Processing")) {
        toon_pipeline_.setupUI();
      }
    }
    ImGui::End();
  }

 private:
  ArcBallController arcball_controller_{};
  ToonFxPipeline toon_pipeline_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
