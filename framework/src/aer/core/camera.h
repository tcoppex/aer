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

  struct Data {
    mat4 projection{};
    mat4 projection_inverse{};
    mat4 view{linalg::translation_matrix(vec3(0.0f, 0.0f, -1.0f))};
    mat4 world{}; // view_inverse
    mat4 view_projection{};
  };
  
 public:
  Camera()
    : controller_{nullptr}
    , fov_(0.0f)
    , width_(0u)
    , height_(0u)
    , linear_params_{0.0f, 0.0f, 0.0f, 0.0f}
  {}

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
  void makePerspective(float fov, uint32_t w, uint32_t h, float znear, float zfar) {
    assert( fov > 0.0f );
    assert( (w > 0) && (h > 0) );
    assert( (zfar - znear) > 0.0f );

    fov_    = fov;
    width_  = w;
    height_ = h;

    auto const ratio = static_cast<float>(width_) / static_cast<float>(height_);

    // Projection matrix.
    matrices_.projection = linalg::perspective_matrix(
      fov_, ratio, znear, zfar, linalg::neg_z, linalg::zero_to_one
    );
    matrices_.projection_inverse = linalg::inverse(matrices_.projection);

    // Linearization parameters.
    float const A  = zfar / (zfar - znear);
    linear_params_ = vec4( znear, zfar, A, - znear * A);

    use_ortho_ = false;
  }

  void makePerspective(float fov, ivec2 const& resolution, float znear, float zfar) {
    makePerspective(fov, resolution.x, resolution.y, znear, zfar);
  }

  /* Create a default perspective projection camera. */
  void makeDefault() {
    makePerspective(
      kDefaultFOV, kDefaultSize, kDefaultSize, kDefaultNear, kDefaultFar
    );
  }

  void makeDefault(ivec2 const& resolution) {
    makePerspective(kDefaultFOV, resolution, kDefaultNear, kDefaultFar);
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
      controller_->calculateViewMatrix(&matrices_.view);
    }
    matrices_.world = linalg::inverse(matrices_.view); //
    matrices_.view_projection = linalg::mul(matrices_.projection, matrices_.view);

    need_rebuild_ = false;
    rebuilt_ = true;
  }

  void set_controller(ViewController *controller) {
    controller_ = controller;
  }

 public:
  /* --- Getters --- */

  [[nodiscard]]
  ViewController* controller() noexcept {
    return controller_;
  }

  [[nodiscard]]
  ViewController const* controller() const noexcept {
    return controller_;
  }

  [[nodiscard]]
  float fov() const noexcept {
    return fov_;
  }

  [[nodiscard]]
  int32_t width() const noexcept {
    return width_;
  }

  [[nodiscard]]
  int32_t height() const noexcept {
    return height_;
  }

  [[nodiscard]]
  float aspect() const {
    return static_cast<float>(width_) / static_cast<float>(height_);
  }

  [[nodiscard]]
  float znear() const noexcept {
    return linear_params_.x;
  }

  [[nodiscard]]
  float zfar() const noexcept {
    return linear_params_.y;
  }

  [[nodiscard]]
  vec4 const& linearization_params() const noexcept {
    return linear_params_;
  }

  [[nodiscard]]
  mat4 const& proj() const noexcept {
    return matrices_.projection;
  }

  [[nodiscard]]
  mat4 const& proj_inverse() const noexcept {
    return matrices_.projection_inverse;
  }

  [[nodiscard]]
  mat4 const& view() const noexcept {
    return matrices_.view;
  }

  [[nodiscard]]
  mat4 const& world() const noexcept {
    return matrices_.world;
  }

  [[nodiscard]]
  mat4 const& viewproj() const noexcept {
    return matrices_.view_projection;
  }

  [[nodiscard]]
  vec3 position() const noexcept {
    return lina::to_vec3(world()[3]); //
  }

  [[nodiscard]]
  vec3 direction() const noexcept {
    return linalg::normalize(-lina::to_vec3(world()[2])); //
  }

  [[nodiscard]]
  vec3 target() const noexcept {
    return controller_ ? controller_->target()
                       : position() + 3.0f * direction() //
                       ;
  }

  [[nodiscard]]
  bool is_ortho() const noexcept {
    return use_ortho_;
  }

  [[nodiscard]]
  bool rebuilt() const noexcept {
    return rebuilt_;
  }

 private:
  ViewController *controller_{};

  float fov_{};
  uint32_t width_{};
  uint32_t height_{};
  vec4 linear_params_{};

  Data matrices_{};

  bool use_ortho_{};
  bool need_rebuild_{true};
  bool rebuilt_{false};
};

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_CAMERA_H_
