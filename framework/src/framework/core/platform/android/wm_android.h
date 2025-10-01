#ifndef VKFRAMEWORK_CORE_PLATEFORM_ANDROID_WM_ANDROID_H_
#define VKFRAMEWORK_CORE_PLATEFORM_ANDROID_WM_ANDROID_H_

#include "framework/core/platform/common.h"
#include "framework/core/platform/wm_interface.h"
#include "framework/core/platform/android/xr_android.h"

/* -------------------------------------------------------------------------- */

struct WMAndroid final : public WMInterface {
 public:
  WMAndroid();

  virtual ~WMAndroid() = default;

  [[nodiscard]]
  bool init(AppData_t app_data) final;

  void shutdown() final;

  [[nodiscard]]
  bool poll(AppData_t app_data) noexcept final;

  void setTitle(std::string_view title) const noexcept final {}

  void close() noexcept final;

  [[nodiscard]]
  uint32_t surface_width() const noexcept final {
    LOG_CHECK(surface_width_ > 0u);
    return surface_width_;
  }

  [[nodiscard]]
  uint32_t surface_height() const noexcept final {
    LOG_CHECK(surface_height_ > 0u);
    return surface_height_;
  }

  [[nodiscard]]
  void* handle() const noexcept final {
    return native_window;
  }

  [[nodiscard]]
  bool isActive() const noexcept final {
    return visible && resumed;
  }

  [[nodiscard]]
  XRPlatformInterface const& xrPlatformInterface() const noexcept final {
    return xr_android_;
  }

  [[nodiscard]]
  std::vector<char const*> vulkanInstanceExtensions() const noexcept final;

  [[nodiscard]]
  VkResult createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const noexcept final;


 public:
  void addAppCmdCallbacks(AppCmdCallbacks *app_cmd_callbacks) {
    appCmdCallbacks_.push_back(app_cmd_callbacks);
  }

  void handleAppCmd(AppData_t app_data, int32_t cmd);

  bool handleInputEvent(AInputEvent *event);

 // -------------------------------------------

 public:
  ANativeWindow *native_window{};

  uint32_t surface_width_{};
  uint32_t surface_height_{};

  bool visible{};
  bool resumed{};
  bool focused{};

 private:
  XRPlatformAndroid xr_android_{};
  std::unique_ptr<AppCmdCallbacks> default_app_callback_{};
  std::vector<AppCmdCallbacks*> appCmdCallbacks_{};
};

/* -------------------------------------------------------------------------- */

#endif // VKFRAMEWORK_CORE_PLATEFORM_ANDROID_WM_ANDROID_H_
