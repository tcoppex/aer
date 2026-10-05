#pragma once

#include "aer/platform/openxr/xr_common.h"

/* -------------------------------------------------------------------------- */

class XRFoveation {
 public:
  XRFoveation() = default;
  ~XRFoveation() { shutdown(); }

  XRFoveation(XRFoveation const&) = delete;
  XRFoveation& operator=(XRFoveation const&) = delete;

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
  bool apply(
    XrSession session,
    XrSwapchain swapchain,
    XrFoveationLevelFB level = XR_FOVEATION_LEVEL_HIGH_FB,
    bool dynamic = false,
    float vertical_offset = 0.0f
  ) {
    if (!pfnCreateProfile_) {
      return false;
    }
    destroyProfile();

    XrFoveationLevelProfileCreateInfoFB level_info{XR_TYPE_FOVEATION_LEVEL_PROFILE_CREATE_INFO_FB};
    level_info.level          = level;
    level_info.verticalOffset = vertical_offset;
    level_info.dynamic        = dynamic ? XR_FOVEATION_DYNAMIC_LEVEL_ENABLED_FB
                                        : XR_FOVEATION_DYNAMIC_DISABLED_FB;

    XrFoveationProfileCreateInfoFB profile_info{XR_TYPE_FOVEATION_PROFILE_CREATE_INFO_FB};
    profile_info.next = &level_info;

    if (XR_FAILED(pfnCreateProfile_(session, &profile_info, &profile_))) {
      LOGW("[OpenXR] xrCreateFoveationProfileFB fails.");
      return false;
    }

    XrSwapchainStateFoveationFB state{XR_TYPE_SWAPCHAIN_STATE_FOVEATION_FB};
    state.profile = profile_;
    if (XR_FAILED(pfnUpdateSwapchain_(swapchain, reinterpret_cast<XrSwapchainStateBaseHeaderFB*>(&state)))) {
      LOGW("[OpenXR] xrUpdateSwapchainFB (foveation) fails.");
      return false;
    }
    return true;
  }

  void shutdown() {
    destroyProfile();
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

  XrInstance instance_{XR_NULL_HANDLE};
  XrFoveationProfileFB profile_{XR_NULL_HANDLE};
  PFN_xrCreateFoveationProfileFB  pfnCreateProfile_{nullptr};
  PFN_xrDestroyFoveationProfileFB pfnDestroyProfile_{nullptr};
  PFN_xrUpdateSwapchainFB         pfnUpdateSwapchain_{nullptr};
};

/* -------------------------------------------------------------------------- */