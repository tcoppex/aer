#ifndef AER_CORE_CAMERA_H_
#define AER_CORE_CAMERA_H_

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

class Camera {
 public:
  static constexpr float kDefaultFOV = lina::radians(60.0f);
  static constexpr float kDefaultNear = 0.1f;
  static constexpr float kDefaultFar = 500.0f;
  static constexpr uint32_t kDefaultSize = 512u;

  class ViewController {
   public:
    virtual ~ViewController() = default;

    virtual bool update(float dt) { return false; }

    virtual void calculateViewMatrix(mat4 *m) = 0;

    virtual vec3 target() const = 0;
  };
  
 public:
  Camera()
    : controller_{nullptr}
    , fov_(0.0f)
    , width_(0u)
    , height_(0u)
    , linear_params_{0.0f, 0.0f, 0.0f, 0.0f}
  {
    view_ = linalg::translation_matrix(vec3(0.0f, 0.0f, -1.0f));
  }

  Camera(ViewController *controller) 
    : Camera()
  {
    controller_ = controller;
  }

  /* Check if the camera settings are valid. */
  bool initialized() const noexcept {
    return (fov_ > 0.0f) && (width_ > 0) && (height_ > 0);
  }

  /* Create a perspective projection matrix. */
  void setPerspective(float fov, uint32_t w, uint32_t h, float znear, float zfar) {
    assert( fov > 0.0f );
    assert( (w > 0) && (h > 0) );
    assert( (zfar - znear) > 0.0f );

    fov_    = fov;
    width_  = w;
    height_ = h;

    // Projection matrix.
    float const ratio = static_cast<float>(width_) / static_cast<float>(height_);
    proj_ = linalg::perspective_matrix(fov_, ratio, znear, zfar, linalg::neg_z, linalg::zero_to_one);
    proj_inverse_ = linalg::inverse(proj_);
    bUseOrtho_ = false;

    // Linearization parameters.
    float const A  = zfar / (zfar - znear);
    linear_params_ = vec4( znear, zfar, A, - znear * A);
  }

  void setPerspective(float fov, ivec2 const& resolution, float znear, float zfar) {
    setPerspective( fov, resolution.x, resolution.y, znear, zfar);
  }

  /* Create a default perspective projection camera. */
  void setDefault() {
    setPerspective( kDefaultFOV, kDefaultSize, kDefaultSize, kDefaultNear, kDefaultFar);
  }

  void setDefault(ivec2 const& resolution) {
    setPerspective( kDefaultFOV, resolution, kDefaultNear, kDefaultFar);
  }

  // Update controller and rebuild all matrices.
  bool update(float dt) {
    rebuilt_ = false;
    if (controller_) {
      need_rebuild_ |= controller_->update(dt);
    }
    if (need_rebuild_) {
      rebuild();
    }
    return rebuilt_;
  }

  // Rebuild all matrices.
  void rebuild(bool bRetrieveView = true) {
    if (controller_ && bRetrieveView) {
      controller_->calculateViewMatrix(&view_);
    }
    world_    = linalg::inverse(view_); //
    viewproj_ = linalg::mul(proj_, view_);

    need_rebuild_ = false;
    rebuilt_ = true;
  }

  void set_controller(ViewController *controller) {
    controller_ = controller;
  }

 public:
  /* --- Getters --- */

  ViewController* controller() {
    return controller_;
  }

  ViewController const* controller() const {
    return controller_;
  }

  float fov() const noexcept {
    return fov_;
  }

  int32_t width() const noexcept {
    return width_;
  }

  int32_t height() const noexcept {
    return height_;
  }

  float aspect() const {
    return static_cast<float>(width_) / static_cast<float>(height_);
  }

  float znear() const noexcept {
    return linear_params_.x;
  }

  float zfar() const noexcept {
    return linear_params_.y;
  }

  vec4 const& linearization_params() const {
    return linear_params_;
  }

  mat4 const& view() const noexcept {
    return view_;
  }

  mat4 const& world() const noexcept {
    return world_;
  }

  mat4 const& proj() const noexcept {
    return proj_;
  }

  mat4 const& proj_inverse() const noexcept {
    return proj_inverse_;
  }

  mat4 const& viewproj() const noexcept {
    return viewproj_;
  }

  vec3 position() const noexcept {
    return lina::to_vec3(world_[3]); //
  }

  vec3 direction() const noexcept {
    return linalg::normalize(-lina::to_vec3(world_[2])); //
  }

  vec3 target() const noexcept {
    return controller_ ? controller_->target()
                       : position() + 3.0f*direction() //
                       ;
  }

  bool is_ortho() const noexcept {
    return bUseOrtho_;
  }

  bool rebuilt() const {
    return rebuilt_;
  }

 private:
  ViewController *controller_{};

  float fov_{};
  uint32_t width_{};
  uint32_t height_{};
  vec4 linear_params_{};

  mat4 view_{};
  mat4 world_{};
  mat4 proj_{};
  mat4 proj_inverse_{};
  mat4 viewproj_{};

  bool bUseOrtho_{};

  bool need_rebuild_{true};
  bool rebuilt_{false};
};

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_CAMERA_H_
