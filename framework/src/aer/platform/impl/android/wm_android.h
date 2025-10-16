#ifndef AER_PLATEFORM_IMPL_ANDROID_WM_ANDROID_H_
#define AER_PLATEFORM_IMPL_ANDROID_WM_ANDROID_H_

#include "aer/platform/common.h"
#include "aer/platform/wm_interface.h"
#include "aer/platform/impl/android/xr_android.h"

/* -------------------------------------------------------------------------- */

struct WMAndroid final : public WMInterface {
 public:
  WMAndroid();

  virtual ~WMAndroid() = default;

  [[nodiscard]]
  bool init(Settings const& settings, AppData_t app_data) final;

  void shutdown() final;

  [[nodiscard]]
  bool poll(AppData_t app_data) noexcept final;

  void set_title(std::string_view title) const noexcept final {}

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
  bool is_active() const noexcept final {
    return visible && resumed;
  }

  [[nodiscard]]
  XRPlatformInterface const& xr_platform_interface() const noexcept final {
    return xr_android_;
  }

  [[nodiscard]]
  std::vector<char const*> vk_instance_extensions() const noexcept final;

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

#endif // AER_PLATEFORM_IMPL_ANDROID_WM_ANDROID_H_
