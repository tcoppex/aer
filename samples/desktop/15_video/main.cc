/* -------------------------------------------------------------------------- */
//
//    15 - Hello Video
//
//
//
/* -------------------------------------------------------------------------- */

// !!! Depends on a local version of gstreamer 1.28 !!!
//
// export GST_PLUGIN_PATH=~/Bin/gstreamer/usr/local/lib/x86_64-linux-gnu/gstreamer-1.0
// export PKG_CONFIG_PATH=~/Bin/gstreamer/usr/local/lib/x86_64-linux-gnu/pkgconfig
// export LD_LIBRARY_PATH=~/Bin/gstreamer/usr/local/lib/x86_64-linux-gnu:$LD_LIBRARY_PATH
//

#include "aer/application.h"
#include "aer/scene/mesh.h"

namespace shader_interop {
#include "shaders/interop.h"
}

#include <gst/gst.h>
#include <gst/vulkan/vulkan.h> // dont put before the framework libs
#include <gst/vulkan/xcb/gstvkdisplay_xcb.h>

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 public:
  using HostData_t = shader_interop::UniformData;

 public:
  SampleApp() = default;
  ~SampleApp() {}

 private:
  bool setup() final {
    gst_init(NULL, NULL);

    gst_pipeline = gst_parse_launch(
        "urisourcebin uri=file:///home/project/aer/assets/videos/sample.mp4 "
        "! decodebin ! videoconvert "
        "! video/x-raw,format=RGBA " //,width=320,height=180 "
        "! vulkanupload ! vulkansink name=my_sink",
        NULL);

    if (!gst_pipeline) {
        std::cerr << "Erreur lors de la création du pipeline" << std::endl;
        return -1;
    }

    GstElement *sink = gst_bin_get_by_name(GST_BIN(gst_pipeline), "my_sink");
    if (sink) {
      // GstVulkanDisplay *display = (GstVulkanDisplay *) gst_vulkan_display_xcb_new(NULL);

      // if (display) {
      //   g_object_set(G_OBJECT(sink), "display", display, NULL);
      //   gst_object_unref(display);
      // } else {
      //   std::cerr << "Could not create XCB display" << std::endl;
      // }

      gst_object_unref(sink);
    }

    // GstVulkanInstance *instance = gst_vulkan_display_get_handle(...);

    gst_element_set_state(gst_pipeline, GST_STATE_PLAYING);

    gst_thread = std::thread([this]() {
      gst_loop = g_main_loop_new(NULL, FALSE);
      g_main_loop_run(gst_loop);
      g_main_loop_unref(gst_loop);
      gst_loop = nullptr;
    });


    // -----

    wm_->set_title("15 - Internet killed the video stars");

    renderer_.set_clear_color({ 0.64f, 0.83f, 0.94f, 1.0f});

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
    cube_.initializeSubmeshDescriptors({
      { Geometry::AttributeType::Position, shader_interop::kAttribLocation_Position },
      { Geometry::AttributeType::Texcoord, shader_interop::kAttribLocation_Texcoord },
      { Geometry::AttributeType::Normal, shader_interop::kAttribLocation_Normal },
    });

    /* Create Buffers & Image(s). */
    {
      auto cmd = context_.createTransientCommandEncoder();

      uniform_buffer_ = cmd.createBufferAndUpload(
        &host_data_, sizeof(host_data_),
        VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
      );

      /* Transfer the cube geometry (vertices attributes & indices) to the device. */
      vertex_buffer_ = cmd.createBufferAndUpload(
        cube_.vertices(),
        VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
      );
      index_buffer_ = cmd.createBufferAndUpload(
        cube_.indices(),
        VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
      );

      /* Load a texture using the current transient command encoder. */
      if (std::string fn{ASSETS_DIR "textures/whynot.png"}; !context_.loadImage2D(cmd, fn, image_)) {
        LOGW("The texture image '{}' could not be found.", fn);
      }

      context_.finishTransientCommandEncoder(cmd);
    }

    /* Alternatively the texture could have been loaded directly using an
     * internal transient command encoder. */
    // context_.loadImage2D(path_to_texture, image_);

    /* We don't need to keep the host data so we can clear them. */
    cube_.clearIndicesAndVertices();

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
          .descriptorCount = 1u,
          .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        },
      });

      descriptor_set_ = context_.createDescriptorSet(descriptor_set_layout_, {
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

    auto shaders{context_.createShaderModules(COMPILED_SHADERS_DIR, {
      "simple.vert.glsl",
      "simple.frag.glsl",
    })};

    /* Setup the graphics pipeline. */
    {
      VkPipelineLayout const pipeline_layout = context_.createPipelineLayout({
        .setLayouts = { descriptor_set_layout_ },
        .pushConstantRanges = {
          {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .size = sizeof(shader_interop::PushConstant),
          }
        },
      });

      graphics_pipeline_ = context_.createGraphicsPipeline(pipeline_layout, {
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
              /* When specifying no format, the default one will be used. */
              // .format = context_.default_color_format(),
              .writeMask = VK_COLOR_COMPONENT_R_BIT
                         | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT
                         | VK_COLOR_COMPONENT_A_BIT
                         ,
            }
          },
        },
        .depthStencil = {
          /* When specifying no format, the default one will be used. */
          // .format = context_.default_depth_stencil_format(),
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

    context_.releaseShaderModules(shaders);

    return true;
  }

  void release() final {
    /* We can simplify destroying resources via the RenderContext::destroyResources
     * method. */
    context_.destroyResources(
      descriptor_set_layout_,
      graphics_pipeline_.layout(),
      graphics_pipeline_,
      image_,
      index_buffer_,
      vertex_buffer_,
      uniform_buffer_
    );

    if (gst_pipeline) {
      gst_element_set_state(gst_pipeline, GST_STATE_NULL);
      gst_object_unref(gst_pipeline);
      gst_pipeline = nullptr;
    }

    if (gst_loop) {
      g_main_loop_quit(gst_loop);
    }

    if (gst_thread.joinable()) {
      gst_thread.join();
    }
  }

  void update(float const dt) final {
    /* Update the world matrix. */
    float const tick{ frame_time() };

    push_constant_.model.worldMatrix = lina::rotation_matrix_axis(
      vec3(3.0f * tick, 0.8f, sinf(tick)),
      tick * 0.62f
    );
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      pass.setViewportScissor(viewport_size_);

      // pass.bindPipeline(graphics_pipeline_);
      // {
      //   pass.bindDescriptorSet(descriptor_set_, VK_SHADER_STAGE_VERTEX_BIT);
      //   pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);

      //   pass.bindVertexBuffer(vertex_buffer_);
      //   pass.bindIndexBuffer(index_buffer_, cube_.vk_index_type());
      //   pass.drawIndexed(cube_.index_count());
      // }
    }
    cmd.endRendering();
  }

 private:
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



  GstElement *gst_pipeline{}; ///
  GMainLoop *gst_loop{};
  std::thread gst_thread;
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
