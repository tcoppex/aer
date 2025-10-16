#include <thread>
#include <chrono>
using namespace std::chrono_literals;

#include "aer/core/events.h"
#include "aer/platform/impl/android/wm_android.h"
#include "aer/platform/impl/android/jni_context.h"

/* -------------------------------------------------------------------------- */

struct DefaultAppCmdCallbacks final : public AppCmdCallbacks {
  DefaultAppCmdCallbacks(WMAndroid *const wma) noexcept
    : wma_(wma)
  {}

  void handleResize(android_app* app, bool signalOnResize = true) {
    int32_t const iw = ANativeWindow_getWidth(app->window);
    int32_t const ih = ANativeWindow_getHeight(app->window);
    auto const w = static_cast<uint32_t>(iw);
    auto const h = static_cast<uint32_t>(ih);
    LOGD("handleResize {} {} // {} {}",
      wma_->surface_width_, wma_->surface_height_,
      w, h
    );

    if (!signalOnResize) {
      wma_->surface_width_ = w;
      wma_->surface_height_ = h;
    }
    else
    {
      // [in some case we need to trigger resize after having set the size]
      // if (wma_->surface_width_ != w || wma_->surface_height_ != h)
      {
        wma_->surface_width_ = w;
        wma_->surface_height_ = h;
        Events::Get().onResize(iw, ih);
      }
    }
  }

  void onInitWindow(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    // (APP_CMD_INIT_WINDOW is called when app->window has a new ANativeWindow)
    if (app->window != nullptr) {
      // We only need to create the display & context once.
      if (wma_->native_window == nullptr) {
        LOGD("> Native Android Window created.");
        wma_->native_window = app->window;
        handleResize(app, false); //
      }
      // wma_->native_window = app->window;

      // we need to specify the surface resolution for the first initialization
      // but we don't want to create everything as it's the true "resize"
      // event that will signal it.
      // handleResize(app, false); //
    }
  }

  void onTermWindow(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->native_window = nullptr;
    wma_->surface_width_ = 0u;
    wma_->surface_height_ = 0u;
  }

  void onWindowResized(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    handleResize(app);
  }

  void onStart(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->visible = true;
  }

  void onResume(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->resumed = true;
  }

  void onPause(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->resumed = false;
  }

  void onStop(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->visible = false;
  }

  void onGainedFocus(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->focused = true;
  }

  void onLostFocus(android_app* app) final {
    LOGD("{}", __FUNCTION__);
    wma_->focused = false;
  }

  // [not always called]
  void onDestroy(android_app* app) final {
    /// [Important]
    /// On Android 2D, to avoid a resizeable activity being destroyed
    /// when resolution changes down, add in its manifest attribute:
    /// android:configChanges="orientation|screenSize|smallestScreenSize|screenLayout|density"
    LOGD("{}", __FUNCTION__);
    wma_->native_window = nullptr;
  }

  private:
    WMAndroid *const wma_{};
};

/* -------------------------------------------------------------------------- */

WMAndroid::WMAndroid()
  : WMInterface()
{
  default_app_callback_ = std::make_unique<DefaultAppCmdCallbacks>(this);
  addAppCmdCallbacks(default_app_callback_.get());
}

// ----------------------------------------------------------------------------

bool WMAndroid::init(Settings const& settings, AppData_t app_data) {
  LOG_CHECK(app_data != nullptr);

  JNIContext::Initialize(app_data);
  xr_android_.init(JNIContext::Get());

  // Mainly an XR thing.
  ANativeActivity_setWindowFlags(app_data->activity, AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

  // [optionnal, rename the thread]
  // prctl(PR_SET_NAME, (long)"VKFramework::Sample::MainThread", 0, 0, 0);

  auto user_data{reinterpret_cast<UserData*>(app_data->userData)};
  user_data->self = this;

  app_data->onAppCmd = [](struct android_app* _app, int32_t cmd) {
    auto user_data = reinterpret_cast<UserData*>(_app->userData);
    auto self = reinterpret_cast<WMAndroid*>(user_data->self);
    self->handleAppCmd(_app, cmd);
  };
  app_data->onInputEvent = [](struct android_app* _app, AInputEvent* event) -> int32_t {
    auto user_data = reinterpret_cast<UserData*>(_app->userData);
    auto self = reinterpret_cast<WMAndroid*>(user_data->self);
    return self->handleInputEvent(event) ? 1 : 0;
  };

  // Wait for the APP_CMD_INIT_WINDOW event which should create the native window.
  while (!app_data->destroyRequested && poll(app_data) && !native_window) {
    std::this_thread::sleep_for(10ms);
  }

  if (!native_window) {
    LOGE("Android native window failed to initialize.");
    return false;
  }

  return true;
}

// ----------------------------------------------------------------------------

void WMAndroid::shutdown() {
  JNIContext::Deinitialize();
}

// ----------------------------------------------------------------------------

bool WMAndroid::poll(AppData_t app_data) noexcept {
  auto user_data = reinterpret_cast<UserData*>(app_data->userData);
  auto xr = user_data->xr;

  while (true) {
    int events{};
    struct android_poll_source* source{nullptr};

    bool const do_not_wait = app_data->destroyRequested
                          || (is_active() && native_window)
                          || (xr && xr->isSessionRunning()) // xxx
                          ;

    auto const poll_id = ALooper_pollOnce(
      (do_not_wait ? 0 : -1), nullptr, &events, (void**)&source
    );

    if (poll_id == ALOOPER_POLL_ERROR) {
      return false;
    }

    if (source) {
      source->process(app_data, source);
    }

    if ((poll_id == ALOOPER_POLL_TIMEOUT) && do_not_wait) {
      break;
    }
  }

  return true;
}

// ----------------------------------------------------------------------------

void WMAndroid::close() noexcept {
  ANativeActivity_finish(JNIContext::Get().activity());
}

// ----------------------------------------------------------------------------

std::vector<char const*> WMAndroid::vk_instance_extensions() const noexcept {
  return {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
  };
}

// ----------------------------------------------------------------------------

VkResult WMAndroid::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const noexcept {
  static PFN_vkCreateAndroidSurfaceKHR fp_vkCreateAndroidSurfaceKHR = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
    vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR")
  );
  if (!fp_vkCreateAndroidSurfaceKHR) {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
  LOG_CHECK(native_window);
  VkAndroidSurfaceCreateInfoKHR const info{
    .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
    .pNext = nullptr,
    .flags = VkAndroidSurfaceCreateFlagsKHR{},
    .window = native_window,
  };
  // [should not be used for OpenXR]
  return fp_vkCreateAndroidSurfaceKHR(instance, &info, nullptr, surface);
}

// ----------------------------------------------------------------------------

void WMAndroid::handleAppCmd(AppData_t app_data, int32_t cmd) {
  for (auto &callbacks : appCmdCallbacks_) {
    switch (cmd) {
      case APP_CMD_INIT_WINDOW:
        callbacks->onInitWindow(app_data);
      break;
      case APP_CMD_TERM_WINDOW:
        callbacks->onTermWindow(app_data);
      break;
      case APP_CMD_WINDOW_RESIZED:
        callbacks->onWindowResized(app_data);
      break;
      case APP_CMD_START:
        callbacks->onStart(app_data);
      break;
      case APP_CMD_RESUME:
        callbacks->onResume(app_data);
      break;
      case APP_CMD_PAUSE:
        callbacks->onPause(app_data);
      break;
      case APP_CMD_STOP:
        callbacks->onStop(app_data);
      break;
      case APP_CMD_GAINED_FOCUS:
        callbacks->onGainedFocus(app_data);
      break;
      case APP_CMD_LOST_FOCUS:
        callbacks->onLostFocus(app_data);
      break;
      case APP_CMD_SAVE_STATE:
        callbacks->onSaveState(app_data);
      break;
      case APP_CMD_DESTROY:
        callbacks->onDestroy(app_data);
      break;
      case APP_CMD_CONFIG_CHANGED:
        LOGV("APP_CMD_CONFIG_CHANGED callback is not supported.");
      break;

      default:
      break;
    }
  }
}

// ----------------------------------------------------------------------------

bool WMAndroid::handleInputEvent(AInputEvent *event) {
  auto& E{Events::Get()};

  auto const action  = AKeyEvent_getAction(event);
  auto const src     = AInputEvent_getSource(event);
  auto const type    = AInputEvent_getType(event);

  switch (type) {
    case AINPUT_EVENT_TYPE_KEY:
    {
      auto const keycode = AKeyEvent_getKeyCode(event);
      if (AKEY_EVENT_ACTION_DOWN == action) {
        E.onKeyPressed(keycode); //
        if (AKEYCODE_BACK == keycode) {}
      } else if (AKEY_EVENT_ACTION_UP == action) {
        E.onKeyReleased(keycode); //
      }
    }
    return true;

    case AINPUT_EVENT_TYPE_MOTION:
      if (AINPUT_SOURCE_CLASS_JOYSTICK == (src & AINPUT_SOURCE_CLASS_MASK)) {
        // (joystick)
      } else {
        auto const ptrIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        auto const motionX = static_cast<int32_t>(AMotionEvent_getX(event, ptrIndex)); //
        auto const motionY = static_cast<int32_t>(AMotionEvent_getY(event, ptrIndex)); //
        auto const keycode = AKeyEvent_getKeyCode(event);
        // auto const motionPointerId = AMotionEvent_getPointerId(event, ptrIndex);
        // auto const motionIsOnScreen = (src == AINPUT_SOURCE_TOUCHSCREEN);

        switch (action & AMOTION_EVENT_ACTION_MASK) {
          case AMOTION_EVENT_ACTION_DOWN:
          case AMOTION_EVENT_ACTION_POINTER_DOWN:
            E.onPointerDown(motionX, motionY, keycode);
          return true;

          case AMOTION_EVENT_ACTION_UP:
          case AMOTION_EVENT_ACTION_POINTER_UP:
            E.onPointerUp(motionX, motionY, keycode);
          return true;

          case AMOTION_EVENT_ACTION_MOVE:
            E.onPointerMove(motionX, motionY);
          return true;

          default:
          break;
        }
      }
    break;

    default:
    break;
  }

  return false;
}

/* -------------------------------------------------------------------------- */
