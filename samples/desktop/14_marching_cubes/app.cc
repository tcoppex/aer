#include "app.h"

namespace shader_interop_rendering {
#include "shaders/interop_rendering.h"
}

/* -------------------------------------------------------------------------- */

bool SampleApp::setup() {
  wm_->set_title("14 - Marching Cubes");

  renderer_.set_clear_color({ 0.52f, 0.45f, 0.65f, 1.0f });

  /* Camera */
  {
    camera_.makePerspective(
      lina::radians(60.0f),
      viewport_size_.width,
      viewport_size_.height,
      0.1f,
      600.0f
    );
    camera_.set_controller(&fly_controller_);
    fly_controller_.set_position(vec3(0.0f, 8.0f, 48.0f));
  }

  /* Skybox */
  {
    auto &skybox = renderer_.skybox();

    skybox.setup(ASSETS_DIR "textures/"
      "scythian_tombs_puresky_2k.hdr"
    );
    skybox.set_hdr_intensity(0.25f);
  }

  /* Uniform buffer. */
  {
    shader_interop_rendering::UniformBufferData host_data{
      .viewMatrix       = camera_.view(),
      .projectionMatrix = camera_.proj(),
    };

    rendering_.uniform_buffer = context_.createBuffer(
      sizeof(host_data),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      , VMA_MEMORY_USAGE_CPU_TO_GPU
    );

    context_.writeBuffer(rendering_.uniform_buffer, host_data); //
  }

  /* Graphics Pipeline. */
  {
    auto const& ds_scene = context_.descriptor_registry()
      .descriptor(DescriptorRegistry::Type::Scene);

    rendering_.layout = context_.createPipelineLayout({
      .setLayouts = { ds_scene.layout },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                      | VK_SHADER_STAGE_FRAGMENT_BIT,
          .size = sizeof(shader_interop_rendering::PushConstant),
        }
      },
    });

    auto shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "rendering.slang");

    rendering_.pipeline = context_.createGraphicsPipeline(rendering_.layout, {
      .vertex = {
        .module = shader.handle,
        .entryPoint = "vertexMain",
      },
      .fragment = {
        .module = shader.handle,
        .entryPoint = "fragmentMain",
        .targets = { GraphicsPipelineDescriptor_t::Fragment::Target{} },
      },
      .depthStencil = {
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      },
      .primitive = {
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .cullMode = VK_CULL_MODE_BACK_BIT,
      }
    });

    context_.releaseShaderModule(shader);
  }

  // ------------------------------------

  /* Chunk Grid. */
  {
    auto & allocator = context_.allocator();
    VkDeviceSize const bytes_before = allocator.getTotalAllocationBytes();

    chunk_grid_.setup(context_);

    VkDeviceSize const bytes_after = allocator.getTotalAllocationBytes();
    VkDeviceSize const net_allocation_delta = bytes_after - bytes_before;
    double const delta_mb = static_cast<double>(net_allocation_delta) / (1024.0 * 1024.0);

    LOGD("ChunkGrid buffer usage: {:.2f} MB allocated.", delta_mb);
  }

  /* Generator. */
  generator_.setup(context_);

  // ------------------------------------

  return true;
}

// ----------------------------------------------------------------------------

void SampleApp::buildUI() {
  ImGui::Begin("Settings");
  {
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Separator();

    ImGui::TextUnformatted("RMB + mouse: look | WASD: move | E/Q: up/down");
    ImGui::TextUnformatted("Shift: faster | Ctrl: slower");
    ImGui::SliderFloat("Speed", fly_controller_.speed_ptr(), 2.0f, 400.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
    ImGui::Separator();

    ImGui::Text("Chunks: %u resident, %u pending", chunk_grid_.resident_count(), static_cast<uint32_t>(chunk_grid_.pending().size()));
    ImGui::Text("Slots: %u free / %u (%u retiring)", chunk_grid_.free_slot_count(), chunk_grid_.size(), chunk_grid_.retired_slot_count());

    int budget = static_cast<int>(chunk_grid_.settings().build_budget);
    if (ImGui::SliderInt("Build budget / frame", &budget, 1, 128)) {
      chunk_grid_.settings().build_budget = static_cast<uint32_t>(budget);
    }
    if (ImGui::Button("Rebuild world")) {
      chunk_grid_.invalidate(frame_index());
    }
  }
  ImGui::End();
}

// ----------------------------------------------------------------------------

void SampleApp::release() {
  context_.destroyResources(
    rendering_.pipeline,
    rendering_.layout,
    rendering_.uniform_buffer
  );
  chunk_grid_.release();
  generator_.release();
}

// ----------------------------------------------------------------------------

void SampleApp::update(float const dt) {
  // Update view matrix.
  context_.writeBuffer(
    rendering_.uniform_buffer,
    offsetof(shader_interop_rendering::UniformBufferData, viewMatrix),
    &camera_.view(),
    0u,
    sizeof(float4x4)
  );

  // Stream the world around the camera.
  {
    chunk_grid_.update(camera_, frame_index());
    generator_.build(chunk_grid_.buffers(), chunk_grid_.pending());
  }
}

// ----------------------------------------------------------------------------

void SampleApp::draw(CommandEncoder const& cmd) {
  auto pass = cmd.beginRendering();
  {
    if (auto const& skybox = renderer_.skybox(); skybox.is_valid()) {
      skybox.render(pass, camera_);
    }

    auto const& chunk_buffers = chunk_grid_.buffers();

    pass.bindPipeline(rendering_.pipeline);
    pass.pushConstant(shader_interop_rendering::PushConstant{
        .modelMatrix  = float4x4(lina::identity),
        .uniformData  = reinterpret_cast<shader_interop_rendering::UniformBufferData*>(rendering_.uniform_buffer.address),
        .vertices     = reinterpret_cast<shader_interop_rendering::MarchingCubeVertex*>(chunk_buffers.vertex.address),
        .vertexOffset = shader_interop_rendering::kChunkMaxVertices,
      },
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    );
    pass.bindIndexBuffer(chunk_buffers.index, VK_INDEX_TYPE_UINT32);

    auto const& ds = context_.descriptor_registry().descriptor(
      DescriptorRegistry::Type::Scene
    );
    pass.bindDescriptorSet(ds.set, VK_SHADER_STAGE_FRAGMENT_BIT);

    pass.drawIndexedIndirect(
      chunk_buffers.draw_indirect, 0u, chunk_grid_.size(), ChunkGrid::kDrawIndexedIndirectSize
    );

  }
  cmd.endRendering();

  drawUI(cmd);
}

/* -------------------------------------------------------------------------- */
