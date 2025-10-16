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
  swapchain_ptr_ = swapchain_ptr;

  initViewResources();

  LOGD(" > Internal Fx");
  {
    skybox_.init(context);
  }
}

// ----------------------------------------------------------------------------

void Renderer::initViewResources() {
  LOG_CHECK( swapchain_ptr_ != nullptr );

  LOGD(" > Frames Resources");

  auto const frame_count = swapchain().image_count();
  LOG_CHECK( frame_count > 0u );
  frames_.resize(frame_count);

  /* Initialize per-frame command buffers. */
  VkCommandPoolCreateInfo const command_pool_create_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .queueFamilyIndex = context_ptr_->queue(Context::TargetQueue::Main).family_index,
  };

  VkDevice const handle = context_ptr_->device();
  for (auto& frame : frames_) {
    CHECK_VK(vkCreateCommandPool(
      handle, &command_pool_create_info, nullptr, &frame.command_pool
    ));
    VkCommandBufferAllocateInfo const cb_alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = frame.command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1u,
    };
    CHECK_VK(vkAllocateCommandBuffers(
      handle, &cb_alloc_info, &frame.command_buffer
    ));
  }

  /* Setup per-frame image buffers. */
  auto const dimension = swapchain().surface_size();
  resize(dimension.width, dimension.height);
}

// ----------------------------------------------------------------------------

void Renderer::releaseViewResources() {
  for (auto & frame : frames_) {
    context_ptr_->freeCommandBuffer(frame.command_pool, frame.command_buffer);
    context_ptr_->destroyCommandPool(frame.command_pool);
    frame.main_rt->release();
  }
}

// ----------------------------------------------------------------------------

void Renderer::release() {
  if (context_ptr_ == nullptr) {
    return;
  }
  skybox_.release(*context_ptr_);
  releaseViewResources();
}

// ----------------------------------------------------------------------------

bool Renderer::resize(uint32_t w, uint32_t h) {
  LOG_CHECK( context_ptr_ != nullptr );
  LOG_CHECK( w > 0 && h > 0 );
  LOGD("[Renderer] Resize Images Buffers ({}, {})", w, h);

  auto const surface_size = VkExtent2D{ w, h };
  auto const layers = swapchain().image_array_size();

  if (frames_[0].main_rt != nullptr) [[likely]] {
    for (auto &frame : frames_) {
      frame.main_rt->resize(w, h);
    }
  } else {
    for (size_t i = 0; i < frames_.size(); ++i) {
      auto &frame = frames_[i];
      frame.main_rt = context_ptr_->createRenderTarget({
        .colors = {{
          .format = color_format(),
          .clear_value = kDefaultColorClearValue
        }},
        .depth_stencil = {
          .format = depth_stencil_format(),
        },
        .size = surface_size,
        .array_size = layers,
        .sample_count = sample_count(),
        .debug_prefix = "Renderer::MainRT_" + std::to_string(i),
      });
    }
  }

  /* Inform the RenderContext of the new size, if any subsystem needs it. */
  context_ptr_->set_default_surface_size(surface_size); //

  return true;
}

// ----------------------------------------------------------------------------

CommandEncoder& Renderer::beginFrame() {
  LOG_CHECK( context_ptr_ != nullptr );

  /* Handle Swapchain resize detection. */
  {
    // (suppose they use the same scale)
    auto const& A = swapchain().surface_size();
    auto const& B = surface_size();
    if (A.width != B.width || A.height != B.height) {
      resize(A.width, A.height);
    }
  }

  /* Acquire next availables image in the swapchain. */
  LOG_CHECK(swapchain_ptr_);
  if (!swapchain().acquireNextImage()) {
    LOGV("{}: Invalid swapchain, should skip current frame.", __FUNCTION__);
  }

  /* Reset the frame command pool to record new command for this frame. */
  auto &frame = frame_resource();
  context_ptr_->resetCommandPool(frame.command_pool);

  // -----------------------
  /* Reset the command buffer wrapper. */
  frame.cmd = CommandEncoder(
    frame.command_buffer,
    static_cast<uint32_t>(Context::TargetQueue::Main),
    context_ptr_->device(),
    &context_ptr_->allocator(), //
    frame.main_rt.get()
  );
  // -----------------------

  frame.cmd.begin();
  return frame.cmd;
}

// ----------------------------------------------------------------------------

void Renderer::applyPostProcess() {
  auto const& frame = frame_resource();
  auto const& dst_img = swapchain().current_image();

  // Blit Color to Swapchain.
  {
    auto const& src_rt = *frame.main_rt;
    auto const& src_img = src_rt.resolve_attachment();
    auto const src_layout = //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                          ;

    uint32_t const layer_count = src_rt.layer_count();
    LOG_CHECK(layer_count == swapchain().image_array_size());

    frame.cmd.transitionImages(
      { src_img },
      VK_IMAGE_LAYOUT_UNDEFINED,
      src_layout,
      layer_count
    );

    frame.cmd.blitImage2D(
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

void Renderer::endFrame() {
  LOG_CHECK( swapchain_ptr_ != nullptr );

  /* Transition the final image then blit to the swapchain frame. */
  if (enable_postprocess_) {
    applyPostProcess();
  }

  auto const& frame = frame_resource();
  frame.cmd.end();

  /* Submit the CommandBuffer to the main queue. */
  auto const& queue = context_ptr_->queue(Context::TargetQueue::Main).queue;
  if (!swapchain().submitFrame(queue, frame.cmd.handle())) {
    LOGV("{}: Invalid swapchain, skip that frame.", __FUNCTION__);
    return; 
  }

  /* Present the swapchain frame's image. */
  swapchain().finishFrame(queue);
  frame_index_ = (frame_index_ + 1u) % swapchain().image_count();
}

// ----------------------------------------------------------------------------

void Renderer::blitColor(
  CommandEncoder const& cmd,
  backend::Image const& src_image
) const noexcept {
  cmd.blitImage2D(
    src_image,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, //
    main_render_target().resolve_attachment(),
    // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, //
    surface_size(),
    swapchain().image_array_size() //
  );
}

// ----------------------------------------------------------------------------

GLTFScene Renderer::loadGLTF(
  std::string_view gltf_filename,
  scene::Mesh::AttributeLocationMap const& attribute_to_location
) {
  if (auto scene = std::make_shared<GPUResources>(*context_ptr_); scene) {
    scene->setup();
    if (scene->loadFile(gltf_filename)) {
      scene->initializeSubmeshDescriptors(attribute_to_location);
      scene->uploadToDevice();
      return scene;
    }
  }
  return {};
}

// ----------------------------------------------------------------------------

GLTFScene Renderer::loadGLTF(std::string_view gltf_filename) {
  return loadGLTF(
    gltf_filename,
    VertexInternal_t::GetDefaultAttributeLocationMap()
  );
}

/* -------------------------------------------------------------------------- */
