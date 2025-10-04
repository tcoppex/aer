#include "aer/application.h"
#include "aer/core/events.h"
#include "aer/platform/window.h"

/* -------------------------------------------------------------------------- */

struct DefaultAppEventCallbacks final : public EventCallbacks {
  using OnResizeCB = std::function<void(int32_t, int32_t)>;

  DefaultAppEventCallbacks(OnResizeCB on_resize_cb)
    : on_resize_cb_(on_resize_cb)
  {}
  ~DefaultAppEventCallbacks() = default;

  void onResize(int w, int h) final {
    on_resize_cb_(w, h);
  }

  OnResizeCB on_resize_cb_{};
};

/* -------------------------------------------------------------------------- */

int Application::run(AppData_t app_data) {
  /* Framework initialization. */
  if (!presetup(app_data)) {
    return EXIT_FAILURE;
  }

  /* User initialization. */
  {
    LOGD("--- App Setup ---");
    if (!setup()) {
      shutdown();
      return EXIT_FAILURE;
    }
    context_.allocator().clear_staging_buffers();
  }

  if (xr_) {
    LOGD("--- Start XR Session ---");
    // xrStartSession();
  }

  mainloop(app_data);

  if (xr_) {
    LOGD("--- End XR Session ---");
    // xrEndSession();
  }

  shutdown();

  return EXIT_SUCCESS;
}

// ----------------------------------------------------------------------------

float Application::elapsed_time() const noexcept {
  auto now{ std::chrono::high_resolution_clock::now() };
  return std::chrono::duration<float>(now - chrono_).count();
}

// ----------------------------------------------------------------------------

bool Application::presetup(AppData_t app_data) {
  auto const app_name = "VkFramework::DefaultAppName"; //

  /* Singletons. */
  {
    Logger::Initialize();
    Events::Initialize();
  }

  LOGD("--- Framework Setup ---");

#if defined(ANDROID)
  app_data->userData = (void*)&user_data_;
#endif

  /* Window manager. */
  if (wm_ = std::make_unique<Window>(); !wm_ || !wm_->init(app_data)) {
    LOGE("Window creation fails");
    shutdown();
    return false;
  }

  // ---------------------------------------

  static constexpr bool kEnableXR = true;
  LOGW("OpenXR is {}.", kEnableXR ? "enabled" : "disabled");

  /* OpenXR */
  if constexpr (kEnableXR) {
    if (xr_ = std::make_unique<OpenXRContext>(); xr_) {
      user_data_.xr = xr_.get(); //
      if (!xr_->init(wm_->xrPlatformInterface(), app_name, xrExtensions())) {
        LOGE("XR initialization fails.");
        shutdown();
        return false;
      }
    }
  }

  /* Vulkan context. */
  if (!context_.init(app_name,
                     wm_->vulkanInstanceExtensions(),
                     vulkanDeviceExtensions(), // [unused]
                     xr_ ? xr_->graphicsInterface() : nullptr))
  {
    LOGE("Vulkan context initialization fails");
    shutdown();
    return false;
  }

  /* Initialize OpenXR Sessions. */
  if (xr_) {
    if (!xr_->initSession()) {
      LOGE("OpenXR sessions initialization fails.");
      shutdown();
      return false;
    }
  }

  /* Surface & Swapchain. */
  if (!reset_swapchain()) {
    LOGE("Surface creation fails");
    shutdown();
    return false;
  }

  // ---------------------------------------

  // Complete OpenXR setup (Controllers & Spaces).
  if (xr_) {
    if (!xr_->completeSetup()) {
      LOGE("OpenXR initialization completion fails.");
      shutdown();
      return false;
    }
  }

  /* Internal Renderer. */
  {
    auto *swapchain_interface = xr_ ? xr_->swapchain_ptr()
                                    : &swapchain_
                                    ;
    renderer_.init(context_, swapchain_interface);
  }

  /* User Interface. */
  if (ui_ = std::make_unique<UIController>(); !ui_ || !ui_->init(renderer_, *wm_)) {
    LOGE("UI creation fails");
    shutdown();
    return false;
  }

  // [~] Capture & handle surface resolution changes.
  {
    auto on_resize = [this](uint32_t w, uint32_t h) {
      context_.device_wait_idle();
      viewport_size_ = {
        .width = w,
        .height = h,
      };
      LOGV("> AppResize (w: {}, h: {})", viewport_size_.width, viewport_size_.height);
      reset_swapchain();
      renderer_.resize(viewport_size_.width, viewport_size_.height);
    };
    default_callbacks_ = std::make_unique<DefaultAppEventCallbacks>(on_resize);
    Events::Get().registerCallbacks(default_callbacks_.get());

    LOGI("> Retrieve original viewport size.");
    viewport_size_ = {
      .width = wm_->surface_width(),
      .height = wm_->surface_height(),
    }; //
    LOGI("> (w: {}, h: {})", viewport_size_.width, viewport_size_.height);
  }

  /* Framework internal data. */
  {
    // Register user's app callbacks.
    Events::Get().registerCallbacks(this);

    // Time tracker.
    chrono_ = std::chrono::high_resolution_clock::now();

    // Initialize the standard C RNG seed, in cases any lib use it.
    rand_seed_ = static_cast<uint32_t>(std::time(nullptr));
    std::srand(rand_seed_);
  }

  LOGD("--------------------------------------------\n");

  return true;
}

// ----------------------------------------------------------------------------

bool Application::next_frame(AppData_t app_data) {
  Events::Get().prepareNextFrame();

  return wm_->poll(app_data)
#if defined(ANDROID)
      && !app_data->destroyRequested
#endif
      ;
}

// ----------------------------------------------------------------------------

void Application::update_timer() noexcept {
  auto const tick = elapsed_time();
  last_frame_time_ = frame_time_;
  frame_time_ = tick;
}

// ----------------------------------------------------------------------------

void Application::update_ui() noexcept {
  ui_->beginFrame();
  build_ui();
  ui_->endFrame();
}

// ----------------------------------------------------------------------------

void Application::mainloop(AppData_t app_data) {
  using frame_fn = std::function<bool()>;

  // ----------------------
  // XR
  // ----------------------
  frame_fn xrFrame{[this]() -> bool {
    xr_->pollEvents();

    if (xr_->shouldStopRender()) {
      return false;
    }
    // update_ui(); //

    if (xr_->isSessionRunning()) [[likely]] {
      xr_->processFrame(
        [this]() { update(delta_time()); },
        [this]() { draw(); }
      );
    } else {
      std::this_thread::sleep_for(10ms);
    }
    return true;
  }};

  // ----------------------
  // Non XR
  // ----------------------
  frame_fn classicFrame{[this]() -> bool {
    if (wm_->isActive()) [[likely]] {
      update_ui();
      update(delta_time());
      draw();
    } else {
      std::this_thread::sleep_for(10ms);
    }
    return true;
  }};

  auto frame{xr_ ? xrFrame : classicFrame};

  LOGD("--- Mainloop ---");
  while (next_frame(app_data)) {
    update_timer();
    if (!frame()) {
      break;
    }
  }
}

// ----------------------------------------------------------------------------

bool Application::reset_swapchain() {
  LOGD("[Reset the Swapchain]");
  
  context_.device_wait_idle();

  // -------------------------------
  // [OpenXR bypass traditionnal Surface+Swapchain creation]
  if (xr_) {
    return xr_->createSwapchains();
  }
  // -------------------------------

  auto surface_creation = VK_SUCCESS;

  /* Release previous swapchain if any, and create the surface when needed. */
  if (VK_NULL_HANDLE == surface_) [[unlikely]] {
    // Initial surface creation.
    surface_creation = CHECK_VK(
      wm_->createWindowSurface(context_.instance(), &surface_)
    );
  } else {
#if defined(ANDROID)
    // On Android we use a new window, so we recreate everything.
    context_.destroy_surface(surface_);
    swapchain_.deinit();
    surface_creation = CHECK_VK(
      wm_->createWindowSurface(context_.instance(), &surface_)
    );
#else
    // On Desktop we can recreate a new swapchain from the old one.
    swapchain_.deinit(true);
#endif
  }
  auto const surface_created{ VK_SUCCESS == surface_creation };

  // Recreate the Swapchain.
  if (surface_created) {
    swapchain_.init(context_, surface_);
  }
  return surface_created;
}

// ----------------------------------------------------------------------------

void Application::shutdown() {
  LOGD("--- Shutdown ---");

  context_.device_wait_idle();

  LOGD("> Application");
  release();

  if (ui_) {
    LOGD("> UI");
    ui_->release(context_);
    ui_.reset();
  }


  LOGD("> Renderer");
  renderer_.deinit();

  if (xr_) {
    LOGD("> OpenXR");
    xr_->terminate();
    xr_.reset();
  } else {
    LOGD("> Swapchain");
    swapchain_.deinit();
    context_.destroy_surface(surface_);
  }

  LOGD("> Render Context");
  context_.deinit();

  if (wm_) {
    LOGD("> Window Manager");
    wm_->shutdown();
    wm_.reset();
  }

  LOGD("> Singletons");
  Events::Deinitialize();
  Logger::Deinitialize();
}

/* -------------------------------------------------------------------------- */
