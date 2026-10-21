#ifndef AER_CORE_FLY_VIEW_CONTROLLER_H_
#define AER_CORE_FLY_VIEW_CONTROLLER_H_

#include "aer/core/view_controller_interface.h"

/* -------------------------------------------------------------------------- */

//
// Free-Fly / First-Person ViewController.
//
//  * Right mouse button + move : look around.
//  * W / S / A / D            : move forward / backward / left / right.
//  * E / Q                    : move up / down (world axis).
//
class FlyViewController : public ViewControllerInterface {
 public:
  static constexpr float kDefaultSpeed{ 15.0f };
  static constexpr float kMouseSensitivity{ 0.0012f };
  static constexpr float kMaxPitch{ 1.55f };
  static constexpr float kAccelerationFactor{ 4.0f };
  static constexpr float kDecelerationFactor{ 1.0f / kAccelerationFactor };

  enum class MovementMode {
    FreeFly,
    FirstPerson
  };

  enum Key : int32_t {
    Key_A = 65,
    Key_D = 68,
    Key_E = 69,
    Key_Q = 81,
    Key_S = 83,
    Key_W = 87,
    Key_LeftShift   = 340,
    Key_LeftControl = 341,
  };

 public:
  FlyViewController()
    : pitch_(0.0f),
      pitch2_(0.0f),
      yaw_(0.0f),
      yaw2_(0.0f),
      position_(0.0f, 0.0f, 0.0f),
      position2_(0.0f, 0.0f, 0.0f),
      speed_(kDefaultSpeed),
      movement_mode_(MovementMode::FreeFly),
      invert_x_(false),
      invert_y_(false)
  {}

  bool update(float dt) override;

  bool update(double const deltatime,
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
              double const mouseY);

  void calculateViewMatrix(mat4 *m, uint32_t /*view_id*/) final;

  [[nodiscard]]
  vec3 target() const final;

  [[nodiscard]]
  vec3 position() const noexcept {
    return position_;
  }

  [[nodiscard]]
  float speed() const noexcept {
    return speed_;
  }

  float* speed_ptr() noexcept {
    return &speed_;
  }

  [[nodiscard]]
  MovementMode movement_mode() const noexcept {
    return movement_mode_;
  }

  void set_movement_mode(MovementMode mode) noexcept {
    movement_mode_ = mode;
  }

  void set_free_fly(bool enabled) noexcept {
    movement_mode_ = enabled ? MovementMode::FreeFly : MovementMode::FirstPerson;
  }

  [[nodiscard]]
  bool is_free_fly() const noexcept {
    return movement_mode_ == MovementMode::FreeFly;
  }

  [[nodiscard]]
  bool is_invert_x() const noexcept {
    return invert_x_;
  }

  void set_invert_x(bool invert) noexcept {
    invert_x_ = invert;
  }

  [[nodiscard]]
  bool is_invert_y() const noexcept {
    return invert_y_;
  }

  void set_invert_y(bool invert) noexcept {
    invert_y_ = invert;
  }

  void set_position(vec3 const& pos, bool bSmooth = false) {
    position2_ = pos;
    if (!bSmooth) {
      position_ = position2_;
    }
  }

  void set_view(double pitch, double yaw, bool bSmooth = false) {
    pitch2_ = pitch;
    yaw2_ = yaw;
    if (!bSmooth) {
      pitch_ = pitch2_;
      yaw_ = yaw2_;
    }
  }

 private:
  void eventMouseMoved(bool const bLook, double const mouseX, double const mouseY);

  void eventMove(
    double const deltatime,
    bool const bForward,
    bool const bBackward,
    bool const bUp,
    bool const bDown,
    bool const bLeft,
    bool const bRight,
    bool const bAccelerate,
    bool const bDecelerate
  );

  void smoothTransition(double const deltatime);

 private:
  static double constexpr kRotateEpsilon = 1.0e-7;
  static double constexpr kAngleModulo = lina::kTwoPi;
  static double constexpr kSmoothingCoeff = 12.0f;

  double last_mouse_x_{};
  double last_mouse_y_{};
  double pitch_{};
  double pitch2_{};
  double yaw_{};
  double yaw2_{};

  vec3 position_{};
  vec3 position2_{};
  float speed_{};

  MovementMode movement_mode_{ MovementMode::FreeFly };
  bool invert_x_{};
  bool invert_y_{};

  mat4 Rmatrix_{ lina::identity };
};

#endif // AER_CORE_FLY_VIEW_CONTROLLER_H_