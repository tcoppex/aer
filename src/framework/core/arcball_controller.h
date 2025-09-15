#ifndef VKFRAMEWORK_CORE_ARCBALL_CONTROLLER_H
#define VKFRAMEWORK_CORE_ARCBALL_CONTROLLER_H

#include "framework/core/camera.h"

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
      yaw_(0.0),
      yaw2_(0.0),
      pitch_(0.0),
      pitch2_(0.0),
      dolly_(kDefaultDollyZ),
      dolly2_(dolly_)
  {}

  void update(float dt) override;

  void getViewMatrix(mat4 *m) final;

  void update(double const deltatime,
              bool const bMoving,
              bool const btnTranslate,
              bool const btnRotate,
              double const mouseX,
              double const mouseY,
              double const wheelDelta)
  {
    if (bMoving) {
      eventMouseMoved(btnTranslate, btnRotate, mouseX, mouseY);
    }
    eventWheel(wheelDelta);
    smoothTransition(deltatime);
    RegulateAngle(pitch_, pitch2_);
    RegulateAngle(yaw_, yaw2_);
  }

  double yaw() const {
    return yaw_;
  }

  double pitch() const {
    return pitch_;
  }

  double dolly() const {
    return dolly_;
  }

  float yawf() const {
    return static_cast<float>(yaw());
  }

  float pitchf() const {
    return static_cast<float>(pitch());
  }

  void setYaw(double const value, bool const bSmooth = kDefaultSmoothTransition) {
    yaw2_ = value;
    yaw_  = (!bSmooth) ? value : yaw_; 
  }

  void setPitch(double const value, bool const bSmooth = kDefaultSmoothTransition, bool const bFastTarget = kDefaultFastestPitchAngle) {
    // use the minimal angle to target.
    double const v1{ value - kAngleModulo };
    double const v2{ value + kAngleModulo };
    double const d0{ std::abs(pitch_ - value) };
    double const d1{ std::abs(pitch_ - v1) };
    double const d2{ std::abs(pitch_ - v2) };
    double const v{ (((d0 < d1) && (d0 < d2)) || !bFastTarget) ? value : (d1 < d2) ? v1 : v2 };

    pitch2_ = v;
    pitch_  = (!bSmooth) ? v : pitch_;
  }

  void setDolly(double const value, bool const bSmooth = kDefaultSmoothTransition) {
    dolly2_ = value;
    if (!bSmooth) {
      dolly_ = dolly2_;
    }
  }

  //---------------
  // [ target is inversed internally, so we change the sign to compensate externally.. fixme]
  vec3 target() const final {
    return -target_; // 
  }

  void setTarget(vec3 const& target, bool const bSmooth = kDefaultSmoothTransition) {
    target2_ = -target;
    if (!bSmooth) {
      target_ = target2_;
    }
  }
  //---------------

  void resetTarget() {
    target_ = vec3(0.0);
    target2_ = vec3(0.0);
  }

  void setView(double const rx, double const ry, bool const bSmooth = kDefaultSmoothTransition, bool const bFastTarget = kDefaultFastestPitchAngle) {
    setYaw(rx, bSmooth);
    setPitch(ry, bSmooth, bFastTarget);
  }

  bool isSideView() const {
    return bSideViewSet_;
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

  static bool constexpr kDefaultSmoothTransition  = true;
  static bool constexpr kDefaultFastestPitchAngle = true;

  double last_mouse_x_;
  double last_mouse_y_;
  double yaw_, yaw2_;
  double pitch_, pitch2_;
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

#endif // VKFRAMEWORK_CORE_ARCBALL_CONTROLLER_H
