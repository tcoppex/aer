#ifndef AER_PLATEFORM_DESKTOP_WINDOW_H_
#define AER_PLATEFORM_DESKTOP_WINDOW_H_

#include "aer/platform/wm_interface.h"
#include "aer/platform/desktop/xr_desktop.h"

#define GLFW_INCLUDE_NONE
#include "GLFW/glfw3.h"

/* -------------------------------------------------------------------------- */

class Window : public WMInterface {
 public:
  Window() = default;

  virtual ~Window() = default;

  [[nodiscard]]
  bool init(Settings const& settings, AppData_t app_data) final;

  void shutdown() final;

  [[nodiscard]]
  bool poll(AppData_t app_data) noexcept final;

  void setTitle(std::string_view title) const noexcept final;

  void close() noexcept final;

  [[nodiscard]]
  uint32_t surface_width() const noexcept final {
    return surface_w_;
  }

  [[nodiscard]]
  uint32_t surface_height() const noexcept final {
    return surface_h_;
  }

  [[nodiscard]]
  void* handle() const noexcept final {
    return window_;
  }

  [[nodiscard]]
  bool isActive() const noexcept final;

  [[nodiscard]]
  XRPlatformInterface const& xrPlatformInterface() const noexcept final {
    return xr_desktop_;
  }

  [[nodiscard]]
  std::vector<char const*> vulkanInstanceExtensions() const noexcept final;

  [[nodiscard]]
  VkResult createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const noexcept final;

 private:
  GLFWwindow* window_{};
  XRPlatformDesktop xr_desktop_{};
  float dpi_scale_{};
  uint32_t surface_w_{};
  uint32_t surface_h_{};
};

/* -------------------------------------------------------------------------- */

#endif  // AER_WINDOW_H_
