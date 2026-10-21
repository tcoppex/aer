#include "aer/core/fly_view_controller.h"
#include "aer/core/events.h"

/* -------------------------------------------------------------------------- */

bool FlyViewController::update(float dt) {
  auto const& e{ Events::Get() };

  bool is_dirty = update(
    static_cast<double>(dt),
    e.has_mouse_moved(),
    e.buttonDown(1),
    e.keyDown(Key_W),
    e.keyDown(Key_S),
    e.keyDown(Key_Q),
    e.keyDown(Key_E),
    e.keyDown(Key_A),
    e.keyDown(Key_D),
    e.keyDown(Key_LeftShift),
    e.keyDown(Key_LeftControl),
    static_cast<double>(e.mouse_x()),
    static_cast<double>(e.mouse_y())
  );

  return is_dirty;
}

// ----------------------------------------------------------------------------

bool FlyViewController::update(
  double const deltatime,
  bool const bMoving,
  bool const bLook,
  bool const bForward,
  bool const bBackward,
  bool const bUp,
  bool const bDown,
  bool const bLeft,
  bool const bRight,
  bool const bAccelerate,
  bool const bDecelerate,
  double const mouseX,
  double const mouseY
) {
  if (bMoving && bLook) {
    eventMouseMoved(bLook, mouseX, mouseY);
  } else {
    last_mouse_x_ = mouseX;
    last_mouse_y_ = mouseY;
  }

  eventMove(deltatime, bForward, bBackward, bUp, bDown, bLeft, bRight, bAccelerate, bDecelerate);
  smoothTransition(deltatime);

  bool is_dirty = (bMoving && bLook)
    || bForward || bBackward || bLeft || bRight
    || !lina::almost_equal<float>(pitch_, pitch2_, 0.01f)
    || !lina::almost_equal<float>(yaw_, yaw2_, 0.01f)
    || !lina::almost_equal(position_, position2_, 0.0001f);

  return is_dirty;
}

// ----------------------------------------------------------------------------

void FlyViewController::calculateViewMatrix(mat4 *m, uint32_t /*view_id*/) {
  auto const Rx = lina::rotation_matrix_x(static_cast<float>(pitch_));
  auto const Ry = lina::rotation_matrix_y(static_cast<float>(yaw_));
  Rmatrix_ = lina::mul(Rx, Ry);

  auto const Tpos = lina::translation_matrix(-position_);

  *m = lina::mul(Rmatrix_, Tpos);
}

// ----------------------------------------------------------------------------

vec3 FlyViewController::target() const {
  auto const forward = lina::row_mul(vec4(0.0f, 0.0f, -1.0f, 0.0f), Rmatrix_).xyz();
  return position_ + forward;
}

// ----------------------------------------------------------------------------

void FlyViewController::eventMouseMoved(
  bool const bLook,
  double const mouseX,
  double const mouseY
) {
  auto const dv_x{ mouseX - last_mouse_x_ };
  auto const dv_y{ mouseY - last_mouse_y_ };
  last_mouse_x_ = mouseX;
  last_mouse_y_ = mouseY;

  if (!bLook || (std::fabs(dv_x) + std::fabs(dv_y)) < kRotateEpsilon) {
    return;
  }

  double const inv_x = invert_x_ ? 1.0 : -1.0;
  double const inv_y = invert_y_ ? 1.0 : -1.0;

  yaw2_ -= dv_x * kMouseSensitivity * inv_x;
  pitch2_ -= dv_y * kMouseSensitivity * inv_y;
  pitch2_ = std::fmax(-kMaxPitch, std::fmin(kMaxPitch, pitch2_));
}

// ----------------------------------------------------------------------------

void FlyViewController::eventMove(
  double const deltatime,
  bool const bForward,
  bool const bBackward,
  bool const bUp,
  bool const bDown,
  bool const bLeft,
  bool const bRight,
  bool const bAccelerate,
  bool const bDecelerate
) {
  if (!bForward && !bBackward && !bLeft && !bRight) {
    return;
  }

  vec3 move_dir(0.0f, 0.0f, 0.0f);
  if (bForward)  move_dir.z -= 1.0f;
  if (bBackward) move_dir.z += 1.0f;
  if (movement_mode_ == MovementMode::FreeFly) {
    if (bDown)     move_dir.y -= 1.0f;
    if (bUp)       move_dir.y += 1.0f;
  }
  if (bLeft)     move_dir.x -= 1.0f;
  if (bRight)    move_dir.x += 1.0f;


  move_dir = lina::normalize(move_dir);

  mat4 rotation;
  if (movement_mode_ == MovementMode::FreeFly) {
    rotation = Rmatrix_;
  } else {
    rotation = lina::rotation_matrix_y(static_cast<float>(yaw_));
  }

  auto const world_move = lina::row_mul(vec4(move_dir, 0.0f), rotation).xyz();

  float speed = speed_;
  if (bAccelerate) speed *= kAccelerationFactor;
  if (bDecelerate) speed *= kDecelerationFactor;

  position2_ += world_move * (speed * static_cast<float>(deltatime));
}

// ----------------------------------------------------------------------------

void FlyViewController::smoothTransition(double const deltatime) {
  auto k{ kSmoothingCoeff * deltatime };
  k = (k > 1.0) ? 1.0 : k;
  yaw_ = lina::lerp(yaw_, yaw2_, k);
  pitch_ = lina::lerp(pitch_, pitch2_, k);
  position_ = lina::lerp(position_, position2_, static_cast<float>(k));
}

/* -------------------------------------------------------------------------- */