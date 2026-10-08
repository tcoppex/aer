#pragma once

#include "aer/platform/openxr/xr_common.h"

/* -------------------------------------------------------------------------- */

struct XRFoveationProfileInterface {
  virtual ~XRFoveationProfileInterface() = default;

  virtual XrFoveationLevelFB level() const noexcept = 0;
  virtual float vertical_offset() const noexcept = 0;
  virtual bool has_dynamic_level() const noexcept = 0;

  virtual void set_level(XrFoveationLevelFB level) noexcept = 0;
  virtual void set_vertical_offset(float vertical_offset) noexcept = 0;
  virtual void set_dynamic_level(bool dynamic_level) noexcept = 0;
};

// ----------------------------------------------------------------------------

class XRFoveationProfile final : public XRFoveationProfileInterface {
 public:
  static constexpr XrFoveationLevelFB kDefaultLevel{
    XR_FOVEATION_LEVEL_MEDIUM_FB
  };
  static constexpr float kDefaultVerticalOffset{
    0.0f
  };
  static constexpr bool kDefaultDynamic{
    false
  };

 public:
  XRFoveationProfile() = default;
  ~XRFoveationProfile() final { shutdown(); }

  XRFoveationProfile(XRFoveationProfile const&) = delete;
  XRFoveationProfile& operator=(XRFoveationProfile const&) = delete;

  [[nodiscard]]
  bool init(XrInstance instance) {
    instance_ = instance;
    bool ok = true;
    ok &= getProc("xrCreateFoveationProfileFB",  (PFN_xrVoidFunction*)&pfnCreateProfile_);
    ok &= getProc("xrDestroyFoveationProfileFB", (PFN_xrVoidFunction*)&pfnDestroyProfile_);
    ok &= getProc("xrUpdateSwapchainFB",         (PFN_xrVoidFunction*)&pfnUpdateSwapchain_);
    return ok;
  }

  [[nodiscard]]
  bool apply(XrSession session, XrSwapchain swapchain) {
    LOG_CHECK(session != XR_NULL_HANDLE);
    LOG_CHECK(swapchain != XR_NULL_HANDLE);

    if (!pfnCreateProfile_) {
      return false;
    }
    destroyProfile();

    auto level_info = XrFoveationLevelProfileCreateInfoFB{
      .type = XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB,
      .next = nullptr,
      .level = level_,
      .verticalOffset = vertical_offset_,
      .dynamic = dynamic_level_ ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB
                                : XR_FOVEATION_DYNAMIC_DISABLED_FB,
    };
    auto profile_info = XrFoveationProfileCreateInfoFB{
      .type = XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB,
      .next = &level_info,
    };
    if (XR_FAILED(pfnCreateProfile_(session, &profile_info, &profile_))) {
      LOGW("[OpenXR] xrCreateFoveationProfileFB fails.");
      return false;
    }
    auto state = XrSwapchainStateFoveationFB{
      .type = XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB,
      .next = nullptr,
      .flags = XrSwapchainStateFoveationFlagsFB{},
      .profile = profile_,
    };
    if (XR_FAILED(pfnUpdateSwapchain_(swapchain, reinterpret_cast<XrSwapchainStateBaseHeaderFB*>(&state)))) {
      LOGW("[OpenXR] xrUpdateSwapchainFB (foveation) fails.");
      return false;
    }

    rebuild_ = false;

    return true;
  }

  void shutdown() {
    destroyProfile();
  }

 public:
  [[nodiscard]]
  XrFoveationLevelFB level() const noexcept {
    return level_;
  }

  [[nodiscard]]
  float vertical_offset() const noexcept {
    return vertical_offset_;
  }

  [[nodiscard]]
  bool has_dynamic_level() const noexcept {
    return dynamic_level_;
  }

  void set_level(XrFoveationLevelFB level) noexcept {
    level_ = level;
    rebuild_ = true;
  }

  void set_vertical_offset(float vertical_offset) noexcept {
    vertical_offset_ = vertical_offset;
    rebuild_ = true;
  }

  void set_dynamic_level(bool dynamic_level) noexcept {
    dynamic_level_ = dynamic_level;
    rebuild_ = true;
  }

 private:
  void destroyProfile() {
    if (profile_ != XR_NULL_HANDLE && pfnDestroyProfile_) {
      pfnDestroyProfile_(profile_);
    }
    profile_ = XR_NULL_HANDLE;
  }

  bool getProc(char const* name, PFN_xrVoidFunction* out) {
    bool const ok = XR_SUCCEEDED(xrGetInstanceProcAddr(instance_, name, out));
    if (!ok) LOGW("{} not found.", name);
    return ok;
  }

 private:
  XrInstance instance_{XR_NULL_HANDLE};
  XrFoveationProfileFB profile_{XR_NULL_HANDLE};

  PFN_xrCreateFoveationProfileFB  pfnCreateProfile_{nullptr};
  PFN_xrDestroyFoveationProfileFB pfnDestroyProfile_{nullptr};
  PFN_xrUpdateSwapchainFB         pfnUpdateSwapchain_{nullptr};

  XrFoveationLevelFB level_{kDefaultLevel};
  float vertical_offset_{kDefaultVerticalOffset};
  bool dynamic_level_{kDefaultDynamic};

  bool rebuild_{false};
};

/* -------------------------------------------------------------------------- */