/* -------------------------------------------------------------------------- */
//
//    07 - Hello Compute
//
//  Where we simulate & sort alpha blended particles on compute shaders
//  via a simple bitonic sorting algorithm.
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
  // The sorting algorithm used support power-of-two sized buffer only.
  static constexpr uint32_t kPointGridSize{ 1024u };
  static constexpr uint32_t kPointGridResolution{ kPointGridSize * kPointGridSize };

  using HostData_t = shader_interop::UniformData;

  enum {
    Compute_Simulation = 0,
    Compute_FillIndices,
    Compute_DotProduct,
    Compute_SortIndices,

    Compute_kCount,
  };

  struct Mesh_t {
    Geometry geo;
    backend::Buffer vertex;
    backend::Buffer index;
  };

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    wm_->setTitle("07 - Nouvelles Vagues");

    renderer_.set_clear_color({ 0.95f, 0.85f, 0.83f, 1.0f });

    /* Initialize the scene data. */
    host_data_.scene.camera = {
      .viewMatrix = linalg::lookat_matrix(
        vec3f(15.0f, 5.0f, 15.0f),
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

    /* Device buffers with initial data. */
    {
      auto cmd = context_.create_transient_command_encoder();

      uniform_buffer_ = cmd.create_buffer_and_upload(
        &host_data_, sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      );

      /* We need to double the size of the device buffers as we
       * will use the vertex one as backup positions and the index one as
       * a ping-pong buffer to sort indices between frames.
       *
       * In a more complex case it would be more interesting to reset the particles
       * via a compute shader, but here it cost about ~4Mb more per 256k particles.
       */
      {
        auto &mesh = point_grid_;
        Geometry::MakePointListPlane(mesh.geo, 16.0f, kPointGridSize, kPointGridSize);

        vertex_buffer_bytesize_ = mesh.geo.get_vertices().size();
        mesh.vertex = cmd.create_buffer_and_upload(
          mesh.geo.get_vertices(),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          vertex_buffer_bytesize_, 2u * vertex_buffer_bytesize_
        );

        index_buffer_bytesize_ = mesh.geo.get_indices().size();
        mesh.index = cmd.create_buffer_and_upload(
          mesh.geo.get_indices(),
          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
          0u, 2u * index_buffer_bytesize_
        );
      }

      context_.finish_transient_command_encoder(cmd);

      /* Buffer used to store the dot product of particles toward the view direction. */
      dot_product_buffer_ = context_.create_buffer(
        point_grid_.geo.get_vertex_count() * sizeof(float),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
      );
    }

    /* Create the shared descriptor set and its layout. */
    {
      VkDescriptorBindingFlags const kDefaultDescBindingFlags{
          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
        | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
        | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      };

      descriptor_set_layout_ = context_.create_descriptor_set_layout({
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
          .descriptorCount = 2u,
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_COMPUTE_BIT
                      ,
          .bindingFlags = kDefaultDescBindingFlags,
        },
        {
          .binding = shader_interop::kDescriptorSetBinding_StorageBuffer_DotProduct,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
                      ,
          .bindingFlags = kDefaultDescBindingFlags,
        },
      });

      descriptor_set_ = context_.create_descriptor_set(descriptor_set_layout_, {
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
        {
          .binding = shader_interop::kDescriptorSetBinding_StorageBuffer_DotProduct,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .buffers = { { dot_product_buffer_.buffer } }
        },
      });
    }

    /* Create a shared pipeline layout. */
    pipeline_layout_ = context_.create_pipeline_layout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .offset = offsetof(shader_interop::PushConstant, graphics),
          .size = sizeof(push_constant_.graphics),
        },
        {
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .offset = offsetof(shader_interop::PushConstant, compute),
          .size = sizeof(push_constant_.compute),
        }
      },
    });

    /* Create the compute pipelines. */
    {
      auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR "sort/", {
        "simulation.comp.glsl",
        "fill_indices.comp.glsl",
        "calculate_dot_product.comp.glsl",
        "sort_indices.comp.glsl",
      })};

      context_.create_compute_pipelines(pipeline_layout_, shaders, compute_pipelines_.data());

      context_.release_shader_modules(shaders);
    }

    /* Create the graphics pipeline. */
    {
      auto shaders{context_.create_shader_modules(COMPILED_SHADERS_DIR, {
        "simple.vert.glsl",
        "simple.frag.glsl",
      })};

      graphics_pipeline_ = renderer_.create_graphics_pipeline(pipeline_layout_, {
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
                  .dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                },
                .alpha =  {
                  .operation = VK_BLEND_OP_ADD,
                  .srcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                  .dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                },
              }
            }
          },
        },
        .depthStencil = {
          .format = renderer_.depth_stencil_format(), //
        },
        .primitive = {
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
          .cullMode = VK_CULL_MODE_BACK_BIT,
        },
      });

      context_.release_shader_modules(shaders);
    }

    return true;
  }

  void release() final {
    for (auto pipeline : compute_pipelines_) {
      context_.destroy_pipeline(pipeline);
    }
    context_.destroy_pipeline(graphics_pipeline_);
    context_.destroy_pipeline_layout(pipeline_layout_);
    context_.destroy_descriptor_set_layout(descriptor_set_layout_);
    context_.destroy_buffer(dot_product_buffer_);
    context_.destroy_buffer(point_grid_.index);
    context_.destroy_buffer(point_grid_.vertex);
    context_.destroy_buffer(uniform_buffer_);
  }

  void draw() final {
    mat4 const world_matrix(
      linalg::mul(
        lina::rotation_matrix_y(0.05f * frame_time()),
        linalg::scaling_matrix(vec3(2.0f))
      )
    );

    auto cmd = renderer_.begin_frame();
    {
      /* Bind the shared descriptor set. */
      cmd.bind_descriptor_set(
        descriptor_set_,
        pipeline_layout_,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT
      );

      /* Simulate & Sort particles */
      {
        auto const nelems = point_grid_.geo.get_vertex_count();

        push_constant_.compute.model.worldMatrix = world_matrix;
        push_constant_.compute.time = frame_time();
        push_constant_.compute.numElems = nelems;

        cmd.push_constant(
          push_constant_.compute,
          pipeline_layout_,
          VK_SHADER_STAGE_COMPUTE_BIT,
          offsetof(shader_interop::PushConstant, compute)
        );

        /// 1) Simulate a simple particle system (Wave simulations).
        cmd.bind_pipeline(compute_pipelines_.at(Compute_Simulation));
        cmd.dispatch<shader_interop::kCompute_Simulation_kernelSize_x>(nelems);

        /// 2) Fill the first part of the indices buffer with continuous indices.
        cmd.bind_pipeline(compute_pipelines_.at(Compute_FillIndices));
        cmd.dispatch<shader_interop::kCompute_FillIndex_kernelSize_x>(nelems);

        cmd.pipeline_buffer_barriers({
          {
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .buffer = point_grid_.vertex.buffer,
          }
        });

        /// 3) Compute the particles dot products against the camera view direction.
        cmd.bind_pipeline(compute_pipelines_.at(Compute_DotProduct));
        cmd.dispatch<shader_interop::kCompute_DotProduct_kernelSize_x>(nelems);

        cmd.pipeline_buffer_barriers({
          {
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .buffer = dot_product_buffer_.buffer,
          },
          {
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .buffer = point_grid_.index.buffer,
          }
        });


        /// 2) Sort indices via their dot products using a bitonic sort.
        cmd.bind_pipeline(compute_pipelines_.at(Compute_SortIndices));
        {
          uint32_t const index_buffer_offset = point_grid_.geo.get_index_count();
          uint32_t index_buffer_binding = 0u;

          // Get trailing bits count.
          uint32_t nsteps = 0u;
          for (uint32_t i = nelems; i > 1u; i >>= 1u) {
            ++nsteps;
          }

          for (uint32_t stage = 0u; stage < nsteps; ++stage)
          {
            uint32_t const max_block_width{ 2u << stage };
            for (uint32_t pass = 0u; pass <= stage; ++pass)
            {
              uint32_t const block_width{ 2u << (stage - pass) };
              uint32_t const read_offset{ index_buffer_offset * index_buffer_binding };
              uint32_t const write_offset{ index_buffer_offset * (index_buffer_binding ^ 1u) };

              push_constant_.compute.readOffset = read_offset;
              push_constant_.compute.writeOffset = write_offset;
              push_constant_.compute.blockWidth = block_width;
              push_constant_.compute.maxBlockWidth = max_block_width;
              cmd.push_constant(
                push_constant_.compute,
                pipeline_layout_,
                VK_SHADER_STAGE_COMPUTE_BIT,
                offsetof(shader_interop::PushConstant, compute)
              );

              cmd.dispatch<shader_interop::kCompute_SortIndex_kernelSize_x>(nelems / 2u);

              cmd.pipeline_buffer_barriers({
                {
                  .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
                  .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                  .buffer = point_grid_.index.buffer,
                  .offset = read_offset * sizeof(uint32_t),
                  .size = index_buffer_bytesize_,
                },
                {
                  .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                  .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                  .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                  .buffer = point_grid_.index.buffer,
                  .offset = write_offset * sizeof(uint32_t),
                  .size = index_buffer_bytesize_,
                },
              });

              index_buffer_binding ^= 1u;
            }
          }
        }

        cmd.pipeline_buffer_barriers({
          {
            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .buffer = point_grid_.index.buffer,
          },
        });
      }

      /* Rendering */
      auto pass = cmd.begin_rendering();
      {
        pass.set_viewport_scissor(viewport_size_);

        pass.bind_pipeline(graphics_pipeline_);

        push_constant_.graphics.model.worldMatrix = world_matrix;
        pass.push_constant(
          push_constant_.graphics,
          VK_SHADER_STAGE_VERTEX_BIT,
          offsetof(shader_interop::PushConstant, graphics)
        );

        pass.draw(4u, point_grid_.geo.get_vertex_count());
      }
      cmd.end_rendering();
    }
    renderer_.end_frame();
  }

 private:
  HostData_t host_data_{};

  Mesh_t point_grid_{};
  uint32_t vertex_buffer_bytesize_{};
  uint32_t index_buffer_bytesize_{};

  backend::Buffer uniform_buffer_{};
  backend::Buffer dot_product_buffer_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};
  shader_interop::PushConstant push_constant_{};

  VkPipelineLayout pipeline_layout_{};

  std::array<Pipeline, Compute_kCount> compute_pipelines_{};
  Pipeline graphics_pipeline_{};
};



// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
