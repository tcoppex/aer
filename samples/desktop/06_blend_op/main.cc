/* -------------------------------------------------------------------------- */
//
//    06 - Hello Blend Particles
//
//  When we show how to manage simple GPU particles that don't use alpha transparency
//  (which need sorting).
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/scene/geometry.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  using HostData_t = shader_interop::UniformData;

  struct Mesh_t {
    Geometry geo;
    backend::Buffer vertex;
    backend::Buffer index;
  };

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.color_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    return S;
  }

  bool setup() final {
    wm_->setTitle("06 - Poussières d'Étoiles");

    renderer_.set_clear_color({ 0.02f, 0.03f, 0.12f, 1.0f });

    /* Initialize the scene data. */
    host_data_.scene.camera = {
      .viewMatrix = linalg::lookat_matrix(
        vec3f(1.0f, 1.25f, 2.0f),
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

    /* Create device buffers. */
    {
      auto cmd = context_.create_transient_command_encoder();

      uniform_buffer_ = cmd.create_buffer_and_upload(
        &host_data_, sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      );

      /* We use storage buffer bit as we will access these attributes procedurally,
       * and not as vertex input.
       */
      {
        auto &mesh = point_grid_;
        Geometry::MakePointListPlane(mesh.geo, 1.0f, 512u, 512u);

        mesh.vertex = cmd.create_buffer_and_upload(
          mesh.geo.vertices(),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
        mesh.index = cmd.create_buffer_and_upload(
          mesh.geo.indices(),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        );
      }

      context_.finish_transient_command_encoder(cmd);
    }

    {
      VkDescriptorBindingFlags const kDefaultDescBindingFlags{
          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
        | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
        | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      };

      graphics_.descriptor_set_layout = context_.create_descriptor_set_layout({
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_COMPUTE_BIT
                      ,
          .bindingFlags = kDefaultDescBindingFlags,
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_StorageBuffer_Position,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_COMPUTE_BIT
                      ,
          .bindingFlags = kDefaultDescBindingFlags,
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_StorageBuffer_Index,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_COMPUTE_BIT
                      ,
          .bindingFlags = kDefaultDescBindingFlags,
        },
      });

      graphics_.descriptor_set = context_.create_descriptor_set(graphics_.descriptor_set_layout, {
        {
          .binding = shader_interop::kDescriptorSetBinding_UniformBuffer,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = { { uniform_buffer_.buffer } }
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_StorageBuffer_Position,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .buffers = { { point_grid_.vertex.buffer } }
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_StorageBuffer_Index,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .buffers = { { point_grid_.index.buffer } }
        },
      });
    }

    graphics_.pipeline_layout = context_.create_pipeline_layout({
      .setLayouts = { graphics_.descriptor_set_layout },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });

    /* Setup the graphics pipeline. */
    {
      auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
        "simple.vert.glsl",
        "simple.frag.glsl",
      })};

      /* Setup a pipeline with additive blend and no depth buffer. */
      graphics_.pipeline = renderer_.create_graphics_pipeline(graphics_.pipeline_layout, {
        .vertex = {
          .module = shaders[0u].module,
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
              .blend = {
                .enable = VK_TRUE,
                .color = {
                  .operation = VK_BLEND_OP_ADD,
                  .srcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                  .dstFactor = VK_BLEND_FACTOR_ONE,
                },
                .alpha =  {
                  .operation = VK_BLEND_OP_ADD,
                  .srcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                  .dstFactor = VK_BLEND_FACTOR_ONE,
                },
              }
            }
          },
        },
        .depthStencil = {
          .format = renderer_.depth_stencil_format(), //
        },
        .primitive = {
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
          /* We disable culling as we let the billboard particles face whatever direction. */
          // .cullMode = VK_CULL_MODE_BACK_BIT,
        },
      });

      context_.release_shader_modules(shaders);
    }

    return true;
  }

  void release() final {
    context_.destroy_pipeline(graphics_.pipeline);
    context_.destroy_descriptor_set_layout(graphics_.descriptor_set_layout);
    context_.destroy_pipeline_layout(graphics_.pipeline_layout);
    context_.destroy_buffer(point_grid_.index);
    context_.destroy_buffer(point_grid_.vertex);
    context_.destroy_buffer(uniform_buffer_);
  }

  void draw(CommandEncoder const& cmd) final {
    mat4 const world_matrix(
      linalg::mul(
        lina::rotation_matrix_y(0.25f * frame_time()),
        linalg::scaling_matrix(vec3(4.0f))
      )
    );

    cmd.bind_descriptor_set(graphics_.descriptor_set, graphics_.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT);

    graphics_.push_constant.model.worldMatrix = world_matrix;
    graphics_.push_constant.time = frame_time();
    cmd.push_constant(graphics_.push_constant, graphics_.pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT);

    auto pass = cmd.begin_rendering();
    {
      pass.set_viewport_scissor(viewport_size_);

      pass.bind_pipeline(graphics_.pipeline);

      /* For each particle vertex we output two triangles to form a quad,
       * so (2 * 3 = 6) vertices per points.
       *
       * As we don't use any vertex inputs, we will just send 6 'empty' vertices
       * instanced the number of positions to transform.
       *
       * For efficiency this is done in a vertex shader instead of a geometry shader.
       */
      pass.draw(6u, point_grid_.geo.vertex_count());
    }
    cmd.end_rendering();
  }

 private:
  HostData_t host_data_{};

  backend::Buffer uniform_buffer_{};
  Mesh_t point_grid_{};

  struct {
    VkDescriptorSetLayout descriptor_set_layout{};
    VkDescriptorSet descriptor_set{};
    shader_interop::PushConstant push_constant{};

    VkPipelineLayout pipeline_layout{};

    Pipeline pipeline{};
  } graphics_;
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
