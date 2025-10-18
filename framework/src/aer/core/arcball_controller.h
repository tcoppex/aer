#ifndef AER_CORE_ARCBALL_CONTROLLER_H_
#define AER_CORE_ARCBALL_CONTROLLER_H_

#include "aer/core/camera.h"

#ifndef ABC_USE_CUSTOM_TARGET
#define ABC_USE_CUSTOM_TARGET   1
#endif

/* -------------------------------------------------------------------------- */

//
// Orbital ViewController for a camera around the Y axis / XZ plane and with 3D panning.
//
class ArcBallController : public Camera::ViewController {
 public:
  static constexpr double kDefaultDollyZ{ 2.5 };

 public:
  ArcBallController()
    : last_mouse_x_(0.0),
      last_mouse_y_(0.0),
      pitch_(0.0),
      pitch2_(0.0),
      yaw_(0.0),
      yaw2_(0.0),
      dolly_(kDefaultDollyZ),
      dolly2_(dolly_)
  {}

  bool update(float dt) override;

  bool update(double const deltatime,
              bool const bMoving,
              bool const btnTranslate,
              bool const btnRotate,
              double const mouseX,
              double const mouseY,
              double const wheelDelta);

  void calculateViewMatrix(mat4 *m, uint32_t /*view_id*/) final;

  [[nodiscard]]
  double yaw() const noexcept {
    return yaw_;
  }

  [[nodiscard]]
  double pitch() const noexcept {
    return pitch_;
  }

  [[nodiscard]]
  double dolly() const noexcept {
    return dolly_;
  }

  [[nodiscard]]
  float pitchf() const noexcept {
    return static_cast<float>(pitch());
  }

  [[nodiscard]]
  float yawf() const noexcept {
    return static_cast<float>(yaw());
  }

  [[nodiscard]]
  bool is_side_view() const noexcept {
    return bSideViewSet_;
  }

  void set_pitch(
    double value,
    bool bSmooth = kDefaultSmoothTransition,
    bool bFastTarget = kDefaultFastestPitchAngle
  ) {
    // use the minimal angle to target.
    double const v1{ value - kAngleModulo };
    double const v2{ value + kAngleModulo };
    double const d0{ std::abs(pitch_ - value) };
    double const d1{ std::abs(pitch_ - v1) };
    double const d2{ std::abs(pitch_ - v2) };
    double const v{
      (((d0 < d1) && (d0 < d2)) || !bFastTarget) ? value : (d1 < d2) ? v1 : v2
    };

    pitch2_ = v;
    pitch_  = (!bSmooth) ? v : pitch_;
  }

  void set_yaw(
    double value,
    bool bSmooth = kDefaultSmoothTransition
  ) {
    yaw2_ = value;
    yaw_  = (!bSmooth) ? value : yaw_;
  }

  void set_dolly(
    double value,
    bool bSmooth = kDefaultSmoothTransition
  ) {
    dolly2_ = value;
    if (!bSmooth) {
      dolly_ = dolly2_;
    }
  }

  //---------------
  // [ target is inversed internally, so we change the sign to compensate externally.. fixme]
  [[nodiscard]]
  vec3 target() const final {
    return -target_; // 
  }

  void set_target(
    vec3 const& target,
    bool bSmooth = kDefaultSmoothTransition
  ) {
    target2_ = -target;
    if (!bSmooth) {
      target_ = target2_;
    }
  }

  void move_target(
    vec3 const& v,
    bool bSmooth = kDefaultSmoothTransition
  ) {
    set_target(v - target2_, bSmooth);
  }
  //---------------

  void resetTarget() {
    target_ = vec3(0.0);
    target2_ = vec3(0.0);
  }

  void set_view(
    double pitch,
    double yaw,
    bool bSmooth = kDefaultSmoothTransition,
    bool bFastTarget = kDefaultFastestPitchAngle
  ) {
    set_pitch(pitch, bSmooth, bFastTarget);
    set_yaw(yaw, bSmooth);
  }

 private:
  // Keep the angles pair into a range specified by kAngleModulo to avoid overflow.
  static
  void RegulateAngle(double &current, double &target) {
    if (fabs(target) >= kAngleModulo) {
      auto const dist{ target - current };
      target = fmod(target, kAngleModulo);
      current = target - dist;
    }
  }

  void eventMouseMoved(bool const btnTranslate,
                       bool const btnRotate,
                       double const mouseX,
                       double const mouseY);

  void eventWheel(double const dx);

  void smoothTransition(double const deltatime);


 private:
  static double constexpr kRotateEpsilon          = 1.0e-7; //

  // Angles modulo value to avoid overflow [should be TwoPi cyclic].
  static double constexpr kAngleModulo            = lina::kTwoPi;

  // Arbitrary damping parameters (Rotation, Translation, and Wheel / Dolly).
  static double constexpr kMouseRAcceleration     = 0.00208f;
  static double constexpr kMouseTAcceleration     = 0.00110f;
  static double constexpr kMouseWAcceleration     = 0.15000f;
  
  // Used to smooth transition, to factor with deltatime.
  static double constexpr kSmoothingCoeff         = 12.0f;

  static bool constexpr kDefaultSmoothTransition  = false;
  static bool constexpr kDefaultFastestPitchAngle = true;

  double last_mouse_x_;
  double last_mouse_y_;
  double pitch_, pitch2_;
  double yaw_, yaw2_;
  double dolly_, dolly2_;

// -------------
  vec3 target_{}; //
  vec3 target2_{}; //

#if ABC_USE_CUSTOM_TARGET
  // [we could avoid keeping the previous rotation matrix].
  mat4 Rmatrix_{ linalg::identity };
#endif
// -------------

  bool bSideViewSet_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_ARCBALL_CONTROLLER_H_
