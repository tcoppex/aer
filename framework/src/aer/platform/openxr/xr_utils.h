#ifndef AER_CORE_PLATFORM_OPENXR_XR_UTILS_H_
#define AER_CORE_PLATFORM_OPENXR_XR_UTILS_H_

#include <utility>

extern "C" {
#include <openxr/openxr.h>
}

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

#if defined(NDEBUG)
#define CHECK_XR(x)       x
#else
#define CHECK_XR(x)                                                \
  ([](XrResult _res) -> XrResult {                                 \
    if (_res < XR_SUCCESS) {                                       \
      LOGE("{} failed [code : {}].", std::string(#x), (int)_res);  \
    }                                                              \
    return _res;                                                   \
  })(x)
#endif

#define CHECK_XR_RET(x) \
  if (CHECK_XR(x) < XR_SUCCESS) [[unlikely]] { return false; }

/* -------------------------------------------------------------------------- */

namespace xrutils {

using SpaceLocationVelocity_t = std::pair<XrSpaceLocation, XrSpaceVelocity>;

mat4f PoseMatrix(XrPosef const& pose);

mat4f ProjectionMatrix(XrFovf const& fov, float nearZ, float farZ);

XrActionStateFloat ActionStateFloat(XrAction action, XrPath subaction_path = XR_NULL_PATH);

XrActionStateVector2f ActionStateVector2f(XrAction action, XrPath subaction_path = XR_NULL_PATH);

XrActionStateBoolean ActionStateBoolean(XrAction action, XrPath subaction_path = XR_NULL_PATH);

float GetFloat(XrSession session, XrAction action, XrPath subaction_path = XR_NULL_PATH);

bool GetBoolean(XrSession session, XrAction action, XrPath subaction_path = XR_NULL_PATH);

bool IsPoseActive(XrSession session, XrAction action, XrPath subaction_path = XR_NULL_PATH);

bool HasButtonSwitched(XrSession session, XrAction action, XrPath subaction_path = XR_NULL_PATH, bool to = true);

SpaceLocationVelocity_t SpaceLocationVelocity(XrSpace space, XrSpace base_space, XrTime time);

bool IsSpaceLocationValid(XrSpaceLocation spaceLocation);

// void ApplyVibration(Side side, float amplitude, XrDuration duration = XR_MIN_HAPTIC_DURATION, float frequency = XR_FREQUENCY_UNSPECIFIED);

} // namespace "xrutils"


/* -------------------------------------------------------------------------- */

#endif // AER_CORE_PLATFORM_OPENXR_XR_UTILS_H_