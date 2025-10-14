#ifndef AER_PLATEFORM_WM_INTERFACE_H_
#define AER_PLATEFORM_WM_INTERFACE_H_

#include "aer/core/common.h"
#include "aer/platform/common.h"
#include "aer/platform/openxr/xr_platform_interface.h"

#include "aer/platform/vulkan/utils.h"

// ----------------------------------------------------------------------------

struct WMInterface {
 public:
  struct Settings {
    uint32_t width{};
    uint32_t height{};
  };

 public:
  WMInterface() = default;

  virtual ~WMInterface() = default;

  [[nodiscard]]
  virtual bool init(Settings const& settings, AppData_t app_data) = 0;

  virtual void shutdown() = 0;

  virtual bool poll(AppData_t app_data) noexcept = 0;

  virtual void setTitle(std::string_view title) const noexcept = 0;

  virtual void close() noexcept = 0;

  [[nodiscard]]
  virtual uint32_t surfaceWidth() const noexcept = 0;

  [[nodiscard]]
  virtual uint32_t surfaceHeight() const noexcept = 0;

  [[nodiscard]]
  virtual void* handle() const noexcept = 0;

  [[nodiscard]]
  virtual bool isActive() const noexcept {
    return true;
  }

  // --- OpenXR ---

  [[nodiscard]]
  virtual XRPlatformInterface const& xrPlatformInterface() const noexcept = 0;

  // --- Vulkan ---

  [[nodiscard]]
  virtual std::vector<char const*> vulkanInstanceExtensions() const noexcept = 0;

  [[nodiscard]]
  virtual VkResult createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const noexcept = 0;
};

/* -------------------------------------------------------------------------- */

#endif  // AER_PLATEFORM_WM_INTERFACE_H_
