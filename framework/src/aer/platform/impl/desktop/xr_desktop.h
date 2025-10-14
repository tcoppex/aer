#ifndef AER_PLATEFORM_IMPL_DESKTOP_XR_DESKTOP_H_
#define AER_PLATEFORM_IMPL_DESKTOP_XR_DESKTOP_H_

#include "aer/platform/common.h"
#include "aer/platform/openxr/xr_platform_interface.h"

/* -------------------------------------------------------------------------- */

struct XRPlatformDesktop final : XRPlatformInterface {
 public:
  XRPlatformDesktop() = default;
  ~XRPlatformDesktop() = default;

  XrLoaderInitInfoBaseHeaderKHR const* loaderInitInfo() const final {
    return nullptr;
  }

  XrBaseInStructure const* instanceCreateInfo() const final {
    return nullptr;
  }

  std::vector<char const*> instanceExtensions() const final {
    return {
    };
  }
};

/* -------------------------------------------------------------------------- */

#endif // AER_PLATEFORM_IMPL_DESKTOP_XR_DESKTOP_H_