#include "aer/platform/openxr/xr_utils.h"

namespace xrutils {

/* -------------------------------------------------------------------------- */

mat4f PoseMatrix(XrPosef const& pose) {
  return linalg::pose_matrix(
    reinterpret_cast<vec4f const&>(pose.orientation),
    reinterpret_cast<vec3f const&>(pose.position)
  );
}

mat4f ProjectionMatrix(XrFovf const& fov, float nearZ, float farZ) {
  return lina::perspective_fov_matrix(
    fov.angleLeft, fov.angleRight, fov.angleDown, fov.angleUp, nearZ, farZ
  );
}

bool IsPoseActive(XrSession session, XrAction action, XrPath subaction_path) {
  XrActionStateGetInfo info{
    .type = XR_TYPE_ACTION_STATE_GET_INFO,
    .action = action,
    .subactionPath = subaction_path,
  };
  XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};
  CHECK_XR(xrGetActionStatePose(session, &info, &state));
  return (XR_TRUE == state.isActive);
}

XrActionStateFloat ActionStateFloat(XrSession session, XrAction action, XrPath subaction_path) {
  XrActionStateGetInfo info{
    .type = XR_TYPE_ACTION_STATE_GET_INFO,
    .action = action,
    .subactionPath = subaction_path,
  };
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  CHECK_XR(xrGetActionStateFloat(session, &info, &state));
  return state;
}

XrActionStateVector2f ActionStateVector2f(XrSession session, XrAction action, XrPath subaction_path) {
  XrActionStateGetInfo info{
    .type = XR_TYPE_ACTION_STATE_GET_INFO,
    .action = action,
    .subactionPath = subaction_path,
  };
  XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
  CHECK_XR(xrGetActionStateVector2f(session, &info, &state));
  return state;
}

XrActionStateBoolean ActionStateBoolean(XrSession session, XrAction action, XrPath subaction_path) {
  XrActionStateGetInfo info{
    .type = XR_TYPE_ACTION_STATE_GET_INFO,
    .action = action,
    .subactionPath = subaction_path,
  };
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  CHECK_XR(xrGetActionStateBoolean(session, &info, &state));
  return state;
}

float GetFloat(XrSession session, XrAction action, XrPath subaction_path) {
  return ActionStateFloat(session, action, subaction_path).currentState;
}

bool GetBoolean(XrSession session, XrAction action, XrPath subaction_path) {
  auto state = ActionStateBoolean(session, action, subaction_path);
  return (XR_TRUE == state.isActive)
      && (XR_TRUE == state.currentState)
      ;
}

bool HasButtonSwitched(XrSession session, XrAction action, XrPath subaction_path, bool to) {
  auto state = ActionStateBoolean(session, action, subaction_path);
  return (XR_TRUE == state.isActive)
      && (XR_TRUE == state.changedSinceLastSync)
      && ((XR_TRUE == state.currentState) == to)
      ;
}

SpaceLocationVelocity_t SpaceLocationVelocity(XrSpace space, XrSpace base_space, XrTime time) {
  XrSpaceVelocity vel{XR_TYPE_SPACE_VELOCITY};
  XrSpaceLocation loc{
    .type = XR_TYPE_SPACE_LOCATION,
    .next = &vel,
  };
  CHECK_XR(xrLocateSpace(space, base_space, time, &loc));
  loc.next = nullptr; // (empty invalid pointer)
  return std::make_pair(loc, vel);
}

bool IsSpaceLocationValid(XrSpaceLocation spaceLocation) {
  return ((spaceLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0
       && (spaceLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0)
        ;
}

// void OpenXRContext::ApplyVibration(XRSide side, float amplitude, XrDuration duration, float frequency) {
//   XrHapticVibration vibration{
//     .type = XR_TYPE_HAPTIC_VIBRATION,
//     .duration = duration,
//     .frequency = frequency,
//     .amplitude = amplitude,
//   };
//   XrHapticActionInfo hapticActionInfo{
//     .type = XR_TYPE_HAPTIC_ACTION_INFO,
//     .action = controls_.action.vibrate,
//     .subactionPath = controls_.hand.path[side],
//   };
//   CHECK_XR(xrApplyHapticFeedback(session_, &hapticActionInfo, reinterpret_cast<XrHapticBaseHeader*>(&vibration)));
// }

/* -------------------------------------------------------------------------- */

} // namespace "xrutils"

