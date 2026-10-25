#include "aer/renderer/renderer.h"
#include "aer/renderer/render_context.h"
#include "aer/renderer/descriptor_registry.h"
#include "aer/renderer/vertex_layout.h"
#include "aer/scene/asset.h"
#include "aer/scene/ecs/instantiate.h"

/* -------------------------------------------------------------------------- */

bool Renderer::init(
  RenderContext& context,
  SwapchainInterface** swapchain_pptr
) {
  LOGD("-- Renderer --");
  LOG_CHECK(swapchain_pptr && *swapchain_pptr);

  context_ptr_ = &context;
  default_frame_handler_.swapchain_pptr = swapchain_pptr;

  initViewResources();

  LOGD(" > Material Fx Registry");
  material_fx_registry_.init(context);

  LOGD(" > Scene Hierarchy");
  scene_tree_.setup();

  // --------------------------------------
  LOGD(" > GPU Pool");
  auto const pool_settings = GpuPool::Settings{
    .vertex_capacity  = kDefaultPoolVertexCapacity,
    .index_capacity   = kDefaultPoolIndexCapacity,
    .max_textures     = DescriptorRegistry::kMaxNumTextures,
    .ray_tracing      = false, //
    .attribute_locations = VertexInternal_t::GetDefaultAttributeLocationMap(),
  };
  if (!gpu_pool_.setup(context, material_fx_registry_, pool_settings)) {
    LOGE("GpuPool failed to initialize.");
    return false;
  }

  LOGD(" > SceneRenderer");
  if (!scene_renderer_.setup(context, gpu_pool_, material_fx_registry_, frames_in_flight())) {
    LOGE("SceneRenderer failed to initialize.");
    return false;
  }
  // --------------------------------------

  LOGD(" > Internal Fx");
  {
    LOGW("[TODO] Skybox");
    // skybox_.init(context);
  }

  return true;
}

// ----------------------------------------------------------------------------

void Renderer::initViewResources() {
  LOG_CHECK( default_frame_handler_.swapchain_pptr != nullptr );
  LOGD(" > Frames Resources");

  auto const& sc = swapchain();
  auto const& surface_size = sc.surface_size();

  bool foveated = context_ptr_->is_foveated_rendering_supported()
               && sc.has_fragment_density_map();

  default_frame_handler_.frames = context_ptr_->createFrameResources(
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
  context_ptr_->releaseFrameResources(default_frame_handler_.frames);
}

// ----------------------------------------------------------------------------

void Renderer::release() {
  if (context_ptr_ == nullptr) {
    return;
  }
  context_ptr_->deviceWaitIdle();

  // skybox_.release(*context_ptr_);
  scene_renderer_.release();
  gpu_pool_.release();
  material_fx_registry_.release();
  releaseViewResources();
}

// ----------------------------------------------------------------------------

bool Renderer::resize(uint32_t w, uint32_t h) {
  LOG_CHECK( context_ptr_ != nullptr );
  LOG_CHECK( w > 0 && h > 0 );
  LOG_CHECK( !default_frame_handler_.frames.empty() );
  LOGD("[Renderer] Resize Images Buffers ({}, {})", w, h);

  auto const surface_size = VkExtent2D{w, h};

  // Resize the render targets.
  context_ptr_->resizeFrameResources(surface_size, default_frame_handler_.frames);

  // Inform the RenderContext of the new size, if any subsystem needs it.
  context_ptr_->set_default_surface_size(surface_size); //

  return true;
}

// ----------------------------------------------------------------------------

CommandEncoder& Renderer::beginFrame(FrameHandler *frame_handler) {
  LOG_CHECK( context_ptr_ != nullptr );

  binded_frame_handler_ = (frame_handler) ? frame_handler : &default_frame_handler_;

  auto &sc    = binded_frame_handler_->swapchain();
  auto &frame = binded_frame_handler_->frame();

  /* Acquire next availables image in the swapchain. */
  if (!sc.acquireNextImage()) {
    LOGV("{}: Invalid swapchain, should skip current frame.", __FUNCTION__);
  }

  /* Reset the frame command pool to record new command for this frame. */
  context_ptr_->resetCommandPool(frame.command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

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

bool Renderer::endFrame() {
  LOG_CHECK( binded_frame_handler_ != nullptr );

  auto &sc    = binded_frame_handler_->swapchain();
  auto &frame = binded_frame_handler_->frame();

  /* Transition the final image then blit to the swapchain frame. */
  if (is_swapchain_blit_enabled()) {
    blitColorToSwapchain(*binded_frame_handler_); //
  }

  frame.cmd.end();

  /* Submit the CommandBuffer to the main queue. */
  auto const& queue = context_ptr_->queue(Context::TargetQueue::Main).handle;
  if (!sc.submitFrame(queue, frame.command_buffer)) {
    LOGV("{}: Invalid swapchain, skip that frame.", __FUNCTION__);
    binded_frame_handler_ = nullptr;
    return false;
  }

  /* Present the swapchain frame's image. */
  if (!sc.finishFrame(queue)) {
    binded_frame_handler_ = nullptr;
    return false;
  }

  binded_frame_handler_->swapIndex();
  binded_frame_handler_ = nullptr;

  return true;
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

void Renderer::blitColorToSwapchain(FrameHandler const& frame_handler) {
  auto const& frame = frame_handler.frame();
  auto const& sc = frame_handler.swapchain();

  auto const& src_rt = *frame.main_rt;
  auto const& src_img = src_rt.resolve_attachment();
  auto const src_layout = //VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        ;

  uint32_t const layer_count = src_rt.layer_count();
  LOG_CHECK(layer_count == sc.image_array_size());

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

    sc.current_image(),
    VK_IMAGE_LAYOUT_UNDEFINED, //
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,

    sc.surface_size(),
    layer_count
  );
}

/* -------------------------------------------------------------------------- */

AssetHandle Renderer::loadAsset(std::string_view filename, scene::LoadOptions options) {
  if (auto asset = scene::LoadGLTF(filename, options); asset) {
    return gpu_pool_.uploadAsset(std::move(*asset));
  }
  LOGE("[Renderer] failed to load asset \"{}\"", filename);
  return {};
}

// ----------------------------------------------------------------------------

bool Renderer::releaseAsset(AssetHandle ah) {
  return gpu_pool_.releaseAsset(ah);
}

// ----------------------------------------------------------------------------

entt::entity Renderer::createInstance(
  AssetHandle ah,
  entt::entity parent,
  mat4 const& local
) {
  auto* da = gpu_pool_.find(ah);
  if (!da || !da->prefab) {
    LOGE("[Renderer] createInstance: stale or invalid asset handle.");
    return entt::null;
  }
  int const scene_index = -1; //
  auto const root = scene::Instantiate(
    scene_tree_, *da->prefab, ah, parent, local, scene_index
  );
  if (root != entt::null) {
    ++da->instance_count;
  }
  return root;
}

// ----------------------------------------------------------------------------

void Renderer::destroyInstance(entt::entity instance_root) {
  auto& reg = scene_tree_.registry;
  if (!reg.valid(instance_root) || !reg.all_of<scene::component::Instance>(instance_root)) {
    LOGE("[Renderer] destroyInstance: entity is not an instance root.");
    return;
  }

  /* As the subtree may contain nested instances (children instantiated under it)
   * we release every count, not just the root's. */
  for (auto const e : scene_tree_.collectSubtree(instance_root)) {
    if (auto const* inst = reg.try_get<scene::component::Instance>(e)) {
      if (auto* da = gpu_pool_.find(inst->asset); da && da->instance_count > 0u) {
        --da->instance_count;
      }
    }
  }
  scene_tree_.destroySubtree(instance_root);
}

// ----------------------------------------------------------------------------

entt::entity Renderer::load(
  std::string_view filename,
  entt::entity parent,
  mat4 const& local,
  scene::LoadOptions options
) {
  auto const ah = loadAsset(filename, options);
  if (ah.valid()) {
    if (auto e = createInstance(ah, parent, local); e != entt::null) {
      return e;
    }
    releaseAsset(ah);
  }
  return entt::null;
}

/* -------------------------------------------------------------------------- */
