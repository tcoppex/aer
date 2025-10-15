#include "aer/renderer/renderer.h"

#include "aer/renderer/render_context.h"
#include "aer/scene/vertex_internal.h"

/* -------------------------------------------------------------------------- */

void Renderer::init(
  RenderContext& context,
  SwapchainInterface** swapchain_ptr
) {
  LOGD("-- Renderer --");

  context_ptr_ = &context;
  device_ = context.device();
  swapchain_ptr_ = swapchain_ptr; //

  init_view_resources();

  // Renderer internal effects.
  {
    LOGD(" > Internal Fx");
    skybox_.init(*this); //
  }
}

// ----------------------------------------------------------------------------

void Renderer::init_view_resources() {
  LOG_CHECK( swapchain_ptr_ != nullptr );

  auto const frame_count = swapchain().imageCount();
  LOG_CHECK( frame_count > 0u );

  LOGD(" > Frames Resources");
  frames_.resize(frame_count);

  // Initialize per-frame command buffers.
  VkCommandPoolCreateInfo const command_pool_create_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = context_ptr_->queue(Context::TargetQueue::Main).family_index,
  };
  for (auto& frame : frames_) {
    CHECK_VK(vkCreateCommandPool(
      device_, &command_pool_create_info, nullptr, &frame.command_pool
    ));
    VkCommandBufferAllocateInfo const cb_alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = frame.command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1u,
    };
    CHECK_VK(vkAllocateCommandBuffers(
      device_, &cb_alloc_info, &frame.command_buffer
    ));
  }

  /* Setup per-frame image buffers. */
  auto const dimension = swapchain().surfaceSize();
  resize(dimension.width, dimension.height);
}

// ----------------------------------------------------------------------------

void Renderer::release_view_resources() {
  for (auto & frame : frames_) {
    vkFreeCommandBuffers(device_, frame.command_pool, 1u, &frame.command_buffer);
    vkDestroyCommandPool(device_, frame.command_pool, nullptr);
    frame.main_rt->release();
  }
}

// ----------------------------------------------------------------------------

void Renderer::release() {
  if (context_ptr_ == nullptr) {
    return;
  }

  skybox_.release(*context_ptr_);
  release_view_resources();
}

// ----------------------------------------------------------------------------

bool Renderer::resize(uint32_t w, uint32_t h) {
  LOG_CHECK( context_ptr_ != nullptr );
  LOG_CHECK( w > 0 && h > 0 );
  LOGD("[Renderer] Resize Images Buffers ({}, {})", w, h);

  auto const layers = swapchain().imageArraySize();

  if (frames_[0].main_rt != nullptr) [[likely]] {
    for (auto &frame : frames_) {
      frame.main_rt->resize(w, h);
    }
  } else {
    for (size_t i = 0; i < frames_.size(); ++i) {
      auto &frame = frames_[i];
      frame.main_rt = context_ptr_->create_render_target({
        .colors = {{
          .format = color_format(),
          .clear_value = kDefaultColorClearValue
        }},
        .depth_stencil = {
          .format = depth_stencil_format(),
        },
        .size = { w, h },
        .array_size = layers,
        .sample_count = sample_count(),
        .debug_prefix = std::string("Renderer::MainRT_" + std::to_string(i)),
      });
    }
  }

  return true;
}

// ----------------------------------------------------------------------------

CommandEncoder& Renderer::begin_frame() {
  LOG_CHECK( device_ != VK_NULL_HANDLE );

  // Handle swapchain resize.
  {
    auto const& A = swapchain().surfaceSize();
    auto const& B = surface_size();
    if (A.width != B.width || A.height != B.height) {
      resize(A.width, A.height);
    }
  }

  // Acquire next availables image in the swapchain.
  LOG_CHECK(swapchain_ptr_);
  if (!swapchain().acquireNextImage()) {
    LOGV("{}: Invalid swapchain, should skip current frame.", __FUNCTION__);
  }

  // Reset the frame command pool to record new command for this frame.
  auto &frame = frame_resource();
  CHECK_VK( vkResetCommandPool(device_, frame.command_pool, 0u) );

  // Reset the command buffer wrapper.
  frame.cmd = CommandEncoder(
    frame.command_buffer,
    static_cast<uint32_t>(Context::TargetQueue::Main),
    device_, //
    &context_ptr_->allocator(), //
    frame.main_rt.get()
  );
  frame.cmd.begin();

  return frame.cmd;
}

// ----------------------------------------------------------------------------

void Renderer::apply_postprocess() {
  auto const& frame = frame_resource();
  auto const& dst_img = swapchain().currentImage();

  // Blit Color to Swapchain.
  {
    auto const& src_rt = *frame.main_rt;
    auto const& src_img = src_rt.resolve_attachment();
    auto const src_layout = //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                          ;

    uint32_t const layer_count = src_rt.layer_count();
    LOG_CHECK(layer_count == swapchain().imageArraySize());

    frame.cmd.transition_images_layout(
      { src_img },
      VK_IMAGE_LAYOUT_UNDEFINED,
      src_layout,
      layer_count
    );

    // The issue may lie in the images barriers settings used.
    // Also, OpenXR might not like a Present_src_khr layout..

    frame.cmd.blit_image_2d(
      src_img, src_layout,
      // -----------------------------
      dst_img, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      // dst_img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      // -----------------------------
      surface_size(),
      layer_count
    );
  }
}

// ----------------------------------------------------------------------------

void Renderer::end_frame() {
  LOG_CHECK( swapchain_ptr_ != nullptr );

  // Transition the final image then blit to the swapchain frame.
  if (enable_postprocess_) {
    apply_postprocess();
  }

  auto const& frame = frame_resource();
  frame.cmd.end();

  // Submit the CommandBuffer to the main queue.
  auto const& queue = context_ptr_->queue(Context::TargetQueue::Main).queue;
  if (!swapchain().submitFrame(queue, frame.cmd.handle())) {
    LOGV("{}: Invalid swapchain, skip that frame.", __FUNCTION__);
    return; 
  }

  // Present the swapchain frame's image.
  swapchain().finishFrame(queue);
  frame_index_ = (frame_index_ + 1u) % swapchain().imageCount();
}

// ----------------------------------------------------------------------------

std::unique_ptr<RenderTarget> Renderer::create_default_render_target(
  uint32_t num_color_outputs
) const {
  auto desc = RenderTarget::Descriptor{
    .depth_stencil = { .format = depth_stencil_format() },
    .size = surface_size(),
    .array_size = swapchain().imageArraySize(), //
    .sample_count = VK_SAMPLE_COUNT_1_BIT, //
  };
  desc.colors.resize(num_color_outputs, {
    .format = color_format(),
    .clear_value = kDefaultColorClearValue,
  });
  return context_ptr_->create_render_target(desc);
}

// ----------------------------------------------------------------------------

GLTFScene Renderer::load_gltf(
  std::string_view gltf_filename,
  scene::Mesh::AttributeLocationMap const& attribute_to_location
) {
  if (GLTFScene scene = std::make_shared<GPUResources>(*this); scene) {
    scene->setup();
    if (scene->load_file(gltf_filename)) {
      scene->initialize_submesh_descriptors(attribute_to_location);
      scene->upload_to_device();
      return scene;
    }
  }

  return {};
}

// ----------------------------------------------------------------------------

GLTFScene Renderer::load_gltf(std::string_view gltf_filename) {
  return load_gltf(
    gltf_filename,
    VertexInternal_t::GetDefaultAttributeLocationMap()
  );
}

// ----------------------------------------------------------------------------

void Renderer::blit_color(
  CommandEncoder const& cmd,
  backend::Image const& src_image
) const noexcept {
  cmd.blit_image_2d(
    src_image,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, //
    main_render_target().resolve_attachment(),
    // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, //
    surface_size(),
    swapchain().imageArraySize() //
  );
}

/* -------------------------------------------------------------------------- */
