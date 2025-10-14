#ifndef AER_CORE_PLATFORM_OPENXR_XR_COMMON_H_
#define AER_CORE_PLATFORM_OPENXR_XR_COMMON_H_

/* -------------------------------------------------------------------------- */

extern "C" {

#if defined(ANDROID)
#include <jni.h>
#endif

#include "aer/platform/backend/vulkan_wrapper.h"

#include <openxr/openxr_platform.h>
#include <openxr/openxr.h>

}

/* -------------------------------------------------------------------------- */

#include <array>
#include <functional>

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

enum XRSide : uint32_t {
  Left,
  Right,
  kNumSide
};

enum XRSpaceId : uint32_t {
  Head,
  Local,
  Stage,
  kNumSpaceId
};

// ----------------------------------------------------------------------------

template<typename T>
using XRSidedBuffer_t = std::array<T, XRSide::kNumSide>;

// using XRImageHandle_t = uint64_t; //

// ----------------------------------------------------------------------------

// [Match Oculus Touch controls]
struct XRControlState_t {
  XrActionSet action_set{};

  struct Handness {
    XRSidedBuffer_t<XrPath> path{};
    XRSidedBuffer_t<XrSpace> aim_space{};
    XRSidedBuffer_t<XrSpace> grip_space{};
  } hand{};

  struct Action {
    XrAction aim_pose{};
    XrAction grip_pose{};
    XrAction index_trigger{};
    XrAction grip_squeeze{};

    XrAction joystick{};
    XrAction click_joystick{};

    XrAction button_x{};
    XrAction button_y{};
    XrAction button_menu{};
    XrAction button_a{};
    XrAction button_b{};
    XrAction button_system{};

    XrAction touch_trigger{};
    XrAction touch_joystick{};
    XrAction touch_x{};
    XrAction touch_y{};
    XrAction touch_a{};
    XrAction touch_b{};

    // output
    XrAction vibrate{};
  } action;

  // Per-frame input values.
  struct Frame {
    XrFrameState state{XR_TYPE_FRAME_STATE};

    // XrPosef head_pose{};

    XRSidedBuffer_t<bool> active{};

    XRSidedBuffer_t<XrPosef> aim_pose{};
    XRSidedBuffer_t<XrPosef> grip_pose{};

    XRSidedBuffer_t<float> grip_squeeze{};

    XRSidedBuffer_t<float> index_trigger{};
    XRSidedBuffer_t<bool> touch_trigger{};

    XRSidedBuffer_t<bool> button_thumbstick{};
    XRSidedBuffer_t<bool> touch_thumbstick{};

    // TODO Joystick XY

    bool button_x{};
    bool button_y{};
    bool touch_x{};
    bool touch_y{};
    bool button_menu{};

    bool button_a{};
    bool button_b{};
    bool touch_a{};
    bool touch_b{};
    bool button_system{};
  } frame;
};

// ----------------------------------------------------------------------------

struct XRFrameData_t {
  std::array<mat4f, XRSide::kNumSide> viewMatrices{};
  std::array<mat4f, XRSide::kNumSide> projMatrices{};
  std::array<mat4f const*, XRSpaceId::kNumSpaceId> spaceMatrices{}; //
  double predictedDisplayTime{};
  bool shouldRender{};
};

// ----------------------------------------------------------------------------

// struct XRFrameView_t {
//   uint32_t viewId{};
//   struct {
//     mat4f view{};
//     mat4f proj{};
//     mat4f viewProj{};
//   } transform;
//   std::array<int32_t, 4u> viewport{0, 0, 0, 0};
//   XRImageHandle_t colorImage{};
//   XRImageHandle_t depthStencilImage{};
// };

// ----------------------------------------------------------------------------

using XRUpdateFunc_t = std::function<void(/*XRFrameData_t const&*/)>;
using XRRenderFunc_t = std::function<void(/*XRFrameView_t const&*/)>;

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_PLATFORM_OPENXR_XR_COMMON_H_