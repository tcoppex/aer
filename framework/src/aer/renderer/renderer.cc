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

  auto const& sc = swapchain();
  auto const& surface_size = sc.surface_size();

  bool foveated = context_ptr_->is_foveated_rendering_supported()
               && sc.has_fragment_density_map();

  frames_ = context_ptr_->createFrameResources(
    sc,
    {
      .colors = {{
        .format = color_format(),
        .clear_value = kDefaultColorClearValue
      }},
      .depth_stencil = {
        .format = depth_stencil_format(),
      },
      .size = surface_size,
      .array_size = sc.image_array_size(),
      .sample_count = sample_count(),
      .foveated = foveated,
      .debug_prefix = "Renderer::MainRT_",
    }
  );
  resize(surface_size.width, surface_size.height);
}

// ----------------------------------------------------------------------------

void Renderer::releaseViewResources() {
  if (context_ptr_ == nullptr) {
    return;
  }
  LOGD(" > Release Frames Resources");
  context_ptr_->releaseFrameResources(frames_);
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
  LOG_CHECK( !frames_.empty() );
  LOGD("[Renderer] Resize Images Buffers ({}, {})", w, h);

  auto const surface_size = VkExtent2D{w, h};

  // Resize the render targets.
  context_ptr_->resizeFrameResources(surface_size, frames_);

  // Inform the RenderContext of the new size, if any subsystem needs it.
  context_ptr_->set_default_surface_size(surface_size); //

  return true;
}

// ----------------------------------------------------------------------------

CommandEncoder& Renderer::beginFrame() {
  LOG_CHECK( context_ptr_ != nullptr );

  auto &sc = swapchain();

  /* Handle Swapchain resize detection. */
  {
    // (suppose they use the same scale)
    auto const& A = sc.surface_size();
    auto const& B = surface_size();
    if (A.width != B.width || A.height != B.height) {
      resize(A.width, A.height);
    }
  }

  /* Acquire next availables image in the swapchain. */
  LOG_CHECK(swapchain_ptr_);
  if (!sc.acquireNextImage()) {
    LOGV("{}: Invalid swapchain, should skip current frame.", __FUNCTION__);
  }

  /* Reset the frame command pool to record new command for this frame. */
  auto &frame = frame_resources();
  context_ptr_->resetCommandPool(frame.command_pool);

  VkImageView fdm = is_foveated_rendering_enabled() ? sc.fragment_density_map_view()
                                                    : VK_NULL_HANDLE
                                                    ;

  // -----------------------
  /* Reset the command buffer wrapper. */
  frame.cmd = CommandEncoder(
    frame.command_buffer,
    static_cast<uint32_t>(Context::TargetQueue::Main),
    context_ptr_->device(),
    &context_ptr_->allocator(), //
    frame.main_rt.get(),
    fdm
  );
  // -----------------------

  frame.cmd.begin();
  return frame.cmd;
}

// ----------------------------------------------------------------------------

void Renderer::endFrame() {
  LOG_CHECK( swapchain_ptr_ != nullptr );

  auto const& frame = frame_resources();

  /* Transition the final image then blit to the swapchain frame. */
  if (is_postprocess_enabled()) {
    applyPostProcess();
  }

  frame.cmd.end();

  /* Submit the CommandBuffer to the main queue. */
  auto const& queue = context_ptr_->queue(Context::TargetQueue::Main).handle;
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
  backend::Image const& src_image,
  VkImageLayout src_layout
) const noexcept {
  auto const& dst_image = main_render_target().resolve_attachment();

  cmd.blitImage2D(
    src_image,
    src_layout,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

    dst_image,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

    surface_size(),
    swapchain().image_array_size()
  );
}

// ----------------------------------------------------------------------------

GLTFScene Renderer::loadGLTF(
  std::string_view gltf_filename,
  scene::Mesh::AttributeLocationMap const& attribute_to_location
) {
  uint32_t const max_frames_in_flights = swapchain_image_count(); //
  auto scene = std::make_shared<GPUResources>(*context_ptr_, max_frames_in_flights);

  if (scene) {
    scene->setup();
    if (scene->loadFile(gltf_filename)) {
      scene->initializeSubmeshDescriptors(attribute_to_location);
      // scene->uploadToDevice(/*max_frames_in_flights*/); // (do it manually instead?)
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

// ----------------------------------------------------------------------------

void Renderer::applyPostProcess() {
  auto const& frame = frame_resources();
  auto const& dst_img = swapchain().current_image();

  // Blit Color to Swapchain.
  auto const& src_rt = *frame.main_rt;
  auto const& src_img = src_rt.resolve_attachment();
  auto const src_layout = //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        ;

  uint32_t const layer_count = src_rt.layer_count();
  LOG_CHECK(layer_count == swapchain().image_array_size());

  frame.cmd.transitionColorImages(
    { src_img },
    VK_IMAGE_LAYOUT_UNDEFINED, //
    src_layout,
    layer_count
  );

  frame.cmd.blitImage2D(
    src_img,
    src_layout,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,

    dst_img,
    VK_IMAGE_LAYOUT_UNDEFINED, //
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

    surface_size(),
    layer_count
  );
}

/* -------------------------------------------------------------------------- */
