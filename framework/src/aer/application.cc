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

int Application::run(AppSettings const& app_settings, AppData_t app_data) {
  settings_ = app_settings;

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
    context_.clearStagingBuffers();
  }

  mainloop(app_data);
  shutdown();

  return EXIT_SUCCESS;
}

// ----------------------------------------------------------------------------

float Application::elapsed_time() const noexcept {
  auto now{ std::chrono::high_resolution_clock::now() };
  return std::chrono::duration<float>(now - chrono_).count();
}

// ----------------------------------------------------------------------------

void Application::drawUI(CommandEncoder const& cmd) {
  ui_->draw(
    cmd,
    renderer_.main_render_target().resolve_attachment().view,
    renderer_.surface_size()
  );
}

// ----------------------------------------------------------------------------

bool Application::presetup(AppData_t app_data) {
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
  wm_ = std::make_unique<Window>();
  if (!wm_ || !wm_->init(settings_.surface, app_data)) {
    LOGE("Window creation fails");
    shutdown();
    return false;
  }

  /* OpenXR */
  if (settings_.use_xr) {
    if (xr_ = std::make_unique<OpenXRContext>(); xr_) {
      user_data_.xr = xr_.get(); //
      if (!xr_->init(wm_->xr_platform_interface(),
                     settings_.app_name,
                     xrExtensions()))
      {
        LOGE("XR initialization fails.");
        shutdown();
        return false;
      }
    }
  }
  LOGD("OpenXR is {}.", xr_ ? "enabled" : "disabled");

  /* Vulkan context. */
  if (!context_.init(settings_.renderer,
                     settings_.app_name,
                     wm_->vk_instance_extensions(),
                     xr_ ? xr_->graphics_interface() : nullptr))
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
  if (!resetSwapchain()) {
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

  /* Default Renderer. */
  renderer_.init(context_, &swapchain_interface_);

  /* User Interface. */
  if (ui_ = std::make_unique<UIController>(); !ui_ || !ui_->init(renderer_, *wm_)) {
    LOGE("UI creation fails");
    shutdown();
    return false;
  }

  // [~] Capture and handle surface resolution change.
  {
    auto on_resize = [this](uint32_t w, uint32_t h) {
      context_.deviceWaitIdle();
      viewport_size_ = {
        .width = w,
        .height = h,
      };
      LOGV("> Surface resize (w: {}, h: {})", viewport_size_.width, viewport_size_.height);
      resetSwapchain();
    };
    default_callbacks_ = std::make_unique<DefaultAppEventCallbacks>(on_resize);
    Events::Get().registerCallbacks(default_callbacks_.get());

    LOGI("> Retrieve original viewport size.");
    viewport_size_ = {
      .width = wm_->surface_width(),
      .height = wm_->surface_height(),
    };
    LOGI("> (w: {}, h: {})", viewport_size_.width, viewport_size_.height);
  }

  /* Framework internal data. */
  {
    // Register user's app callbacks.
    Events::Get().registerCallbacks(this);

    // Time tracker.
    chrono_ = std::chrono::high_resolution_clock::now();

    // Initialize the standard C RNG seed, in cases any lib use it.
    rng_seed_ = static_cast<uint32_t>(std::time(nullptr));
    std::srand(rng_seed_);
  }

  LOGD("--------------------------------------------\n");

  return true;
}

// ----------------------------------------------------------------------------

bool Application::nextFrame(AppData_t app_data) {
  Events::Get().prepareNextFrame();

  return wm_->poll(app_data)
#if defined(ANDROID)
      && !app_data->destroyRequested
#endif
      ;
}

// ----------------------------------------------------------------------------

void Application::updateTimer() noexcept {
  auto const tick = elapsed_time();
  last_frame_time_ = frame_time_;
  frame_time_ = tick;
}

// ----------------------------------------------------------------------------

void Application::updateUI() noexcept {
  ui_->beginFrame();
  buildUI();
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

    if (xr_->isSessionRunning()) [[likely]] {
      xr_->processFrame(
        [this]() {
          updateUI();
          update(delta_time());
        },
        [this]() {
          auto const& cmd = renderer_.beginFrame();
          draw(cmd);
          renderer_.endFrame();
        }
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
    if (wm_->is_active()) [[likely]] {
      updateUI();
      update(delta_time());
      auto const& cmd = renderer_.beginFrame();
      draw(cmd);
      renderer_.endFrame();
    } else {
      std::this_thread::sleep_for(10ms);
    }
    return true;
  }};

  auto frame{xr_ ? xrFrame : classicFrame};

  LOGD("--- Mainloop ---");
  while (nextFrame(app_data)) {
    updateTimer();
    if (!frame()) {
      break;
    }
  }
}

// ----------------------------------------------------------------------------

bool Application::resetSwapchain() {
  LOGD("[Reset the Swapchain]");
  
  context_.deviceWaitIdle();
  bool bSuccess = false;

  if (!xr_) {
    auto surface_creation = VK_SUCCESS;

    /* Release previous swapchain if any, and create the surface when needed. */
    if (VK_NULL_HANDLE != surface_) [[likely]] {
#if defined(ANDROID)
      // On Android we use a new window, so we recreate everything.
      context_.destroySurface(surface_);
      swapchain_.release();
      surface_creation = CHECK_VK(
        wm_->createWindowSurface(context_.instance(), &surface_)
      );
#else
      // On Desktop we can recreate a new swapchain from the old one.
      swapchain_.release(Swapchain::kKeepPreviousSwapchain);
#endif
    } else {
      // First surface creation.
      surface_creation = CHECK_VK(
        wm_->createWindowSurface(context_.instance(), &surface_)
      );
    }

    // Recreate the Swapchain.
    if (VK_SUCCESS == surface_creation) {
      bSuccess = swapchain_.init(context_, surface_);
    }
  } else {
    // [OpenXR bypass traditionnal Vulkan surface + swapchain creation]
    bSuccess = xr_->resetSwapchain();
  }

  // Update the pointer to the underlying swapchain.
  swapchain_interface_ = xr_ ? xr_->swapchain_interface()
                             : &swapchain_
                             ;
  return bSuccess;
}

// ----------------------------------------------------------------------------

void Application::shutdown() {
  LOGD("--- Shutdown ---");

  context_.deviceWaitIdle();

  LOGD("> Application");
  release();

  if (ui_) {
    LOGD("> UI");
    ui_->release(context_);
    ui_.reset();
  }

  LOGD("> Renderer");
  renderer_.release();

  if (xr_) {
    LOGD("> OpenXR");
    xr_->shutdown();
    xr_.reset();
  } else {
    LOGD("> Swapchain");
    swapchain_.release();
    context_.destroySurface(surface_);
  }

  LOGD("> Device Context");
  context_.release();

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
