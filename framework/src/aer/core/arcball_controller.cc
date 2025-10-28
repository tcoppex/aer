#include "aer/core/arcball_controller.h"
#include "aer/core/events.h" //

/* -------------------------------------------------------------------------- */

bool ArcBallController::update(float dt) {
  auto const& e{ Events::Get() };

  bool is_dirty = update(
    dt,
    e.mouseMoved(),
    e.buttonDown(2) || e.keyDown(342),  //   e.bMiddleMouse || e.bLeftAlt,
    e.buttonDown(1) || e.keyDown(340),  //   e.bRightMouse || e.bLeftShift,
    (double)e.mouseX(),
    (double)e.mouseY(),
    (double)e.wheelDelta()
  );

  /// Event Signal processing.

  // Keypad quick views.
  auto const pi       { M_PI };
  auto const half_pi  { pi / 2 };
  auto const rshift   { half_pi / 4 };
  auto const rx       { pitch2_ };
  auto const ry       { yaw2_ };

  // Check that the last char was entered with the keypad.
  // [Should be rewritten to test true keysym against GLFW_KEY_KP_*]
  // if (!e.bKeypad) {
  //   return;
  // }
  bool is_dirty2 = true;
  bool const kSmoothTransition = true;
  bool const kFastTarget = true;
  switch (e.lastInputChar()) {
    // "Default" view.
    case '0': //GLFW_KEY_0:
      resetTarget();
      set_view(pi / 16.0, pi / 8.0, kSmoothTransition, kFastTarget);
      set_dolly(6.0, kSmoothTransition);
    break;

    // Side axis views.
    case '1': //GLFW_KEY_1:
      set_view(0.0, 0.0, kSmoothTransition, kFastTarget);
      //resetTarget();
      bSideViewSet_ = true;
    break;

    case '3': //GLFW_KEY_3:
      set_view(0.0, -half_pi, kSmoothTransition, kFastTarget);
      //resetTarget();
      bSideViewSet_ = true;
    break;

    case '7': //GLFW_KEY_7:
      set_view(half_pi, 0.0, kSmoothTransition, kFastTarget);
      //resetTarget();
      bSideViewSet_ = true;
    break;

    // Quick axis rotations.
    case '2': //GLFW_KEY_2:
      set_view(rx - rshift, ry, kSmoothTransition, kFastTarget);
    break;

    case '4': //GLFW_KEY_4:
      set_view(rx, ry + rshift, kSmoothTransition, kFastTarget);
    break;

    case '6': //GLFW_KEY_6:
      set_view(rx, ry - rshift, kSmoothTransition, kFastTarget);
    break;

    case '8': //GLFW_KEY_8:
      set_view(rx + rshift, ry, kSmoothTransition, kFastTarget);
    break;

    // Reverse Y-axis view.
    case '9': //GLFW_KEY_9:
      set_view(rx, ry + pi, kSmoothTransition, !kFastTarget);
    break;

    default:
      is_dirty2 = false;
    break;
  }

  return is_dirty || is_dirty2;
}

// ----------------------------------------------------------------------------

bool ArcBallController::update(
  double const deltatime,
  bool const bMoving,
  bool const btnTranslate,
  bool const btnRotate,
  double const mouseX,
  double const mouseY,
  double const wheelDelta
) {
  if (bMoving) {
    eventMouseMoved(btnTranslate, btnRotate, mouseX, mouseY);
  }
  eventWheel(wheelDelta);
  smoothTransition(deltatime);

  bool is_dirty = (bMoving && (btnTranslate || btnRotate))
    || !lina::almost_equal<float>(pitch_, pitch2_, 0.01f)
    || !lina::almost_equal<float>(yaw_, yaw2_, 0.01f)
    || !lina::almost_equal<float>(dolly_, dolly2_, 0.01f)
    || !lina::almost_equal(target_, target2_, 0.0001f)
    ;

  RegulateAngle(pitch_, pitch2_);
  RegulateAngle(yaw_, yaw2_);

  return is_dirty;
}

// ----------------------------------------------------------------------------

void ArcBallController::calculateViewMatrix(mat4 *m, uint32_t /*view_id*/) {
  static_assert( sizeof(mat4) == (16u * sizeof(float)) );

#if ABC_USE_CUSTOM_TARGET
  // This matrix will orbit around the front of the camera.

  auto const dolly = vec3( 0.0f, 0.0f, - static_cast<float>(dolly_));
  auto const Tdolly = linalg::translation_matrix(dolly);

  auto const Tpan = linalg::translation_matrix(target_);

  auto const Rx = lina::rotation_matrix_x(pitchf());
  auto const Ry = lina::rotation_matrix_y(yawf());

  Rmatrix_ = linalg::mul(Rx, Ry);

  *m = linalg::mul(
    linalg::mul(Tdolly, Rmatrix_),
    Tpan
  );

#else
  // This matrix will always orbit around the space center.

  //   view.setToIdentity();
  //   view.translate(tx_, ty_, -dolly_);
  //   view.rotate(camera_.yaw,    1.0f, 0.0f, 0.0f);
  //   view.rotate(camera_.pitch,  0.0f, 1.0f, 0.0f);

  double const cy{ cosf(yaw_) };
  double const sy{ sinf(yaw_) };
  double const cp{ cosf(pitch_) };
  double const sp{ sinf(pitch_) };

  mat4f &mm = *m;

  mm[ 0].x = cp;
  mm[ 0].y = sy * sp;
  mm[ 0].z = -sp * cy;
  mm[ 0].w = 0.0f;

  mm[ 1].x = 0.0f;
  mm[ 1].y = cy;
  mm[ 1].z = sy;
  mm[ 1].w = 0.0f;

  mm[ 2].x = sp;
  mm[ 2].y = -sy * cp;
  mm[ 2].z = cy * cp;
  mm[ 2].w = 0.0f;

  mm[ 3].x = target_.x;
  mm[ 3].y = target_.y;
  mm[ 3].z = static_cast<double>(-dolly_);
  mm[ 3].w = 1.0f;
#endif
}

// ----------------------------------------------------------------------------

void ArcBallController::eventMouseMoved(
  bool const btnTranslate,
  bool const btnRotate,
  double const mouseX,
  double const mouseY
) {
  auto const dv_x{ mouseX - last_mouse_x_ };
  auto const dv_y{ mouseY - last_mouse_y_ };
  last_mouse_x_ = mouseX;
  last_mouse_y_ = mouseY;

  if ((std::fabs(dv_x) + std::fabs(dv_y)) < kRotateEpsilon) {
    return;
  }

  if (btnRotate) {
    pitch2_ += dv_y * kMouseRAcceleration; //
    yaw2_ += dv_x * kMouseRAcceleration; //
    bSideViewSet_ = false;
  }

  if (btnTranslate) {
    auto const acc{ dolly2_ * kMouseTAcceleration }; //

    double const tx = + dv_x * acc;
    double const ty = - dv_y * acc;

#if ABC_USE_CUSTOM_TARGET
    auto t = lina::mul(vec4(tx, ty, 0.0f, 0.0f), Rmatrix_);
    target_ += t.xyz();
    target2_ = target_;
#else
    target2_.x += tx;
    target2_.y += ty;
#endif
  }
}

// ----------------------------------------------------------------------------

void ArcBallController::eventWheel(double const dx) {
  auto const sign{ (std::fabs(dx) > 1.e-5) ? ((dx > 0.0) ? -1.0 : 1.0) : 0.0 };
  dolly2_ *= (1.0 + sign * kMouseWAcceleration); //
}

// ----------------------------------------------------------------------------

float convergeEaseIn(float current, float target, float alpha)
{
  float d = target - current;
  float step = d * pow(std::fabs(d), alpha);
  return current + step;
}


void ArcBallController::smoothTransition(double const deltatime) {
  // should filter / bias the final signal as it will keep a small jittering aliasing value
  // due to temporal composition, or better yet : keep an anim timecode for smoother control.
  auto k{ kSmoothingCoeff * deltatime };
  k = (k > 1.0) ? 1.0 : k;
  yaw_   = linalg::lerp(yaw_, yaw2_, k);
  pitch_ = linalg::lerp(pitch_, pitch2_, k);
  dolly_ = linalg::lerp(dolly_, dolly2_, k);
  target_ = linalg::lerp(target_, target2_, static_cast<float>(k));
}

/* -------------------------------------------------------------------------- */
