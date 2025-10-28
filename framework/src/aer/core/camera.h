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

    /* Handle event inputs, return true when the view matrices has changed. */
    virtual bool update(float dt) { return false; }

    /* Retrieve the new view matrix. */
    virtual void calculateViewMatrix(mat4 *m, uint32_t view_id = 0u) = 0;

    /* Number of view supported by the controller, should be max 2. */
    virtual uint32_t view_count() const noexcept { return 1u; }

    virtual vec3 target() const = 0; //
  };

  struct Transform {
    mat4 projection{linalg::identity};
    mat4 projection_inverse{linalg::identity};
    mat4 view{linalg::translation_matrix(vec3(0.0f, 0.0f, -1.0f))};
    mat4 world{linalg::identity}; // view_inverse
    mat4 view_projection{linalg::identity};

    [[nodiscard]]
    vec3 position() const noexcept {
      return lina::to_vec3(world[3]); //
    }

    [[nodiscard]]
    vec3 direction() const {
      return linalg::normalize(-lina::to_vec3(world[2])); //
    }
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
    LOG_CHECK( fov > 0.0f );
    LOG_CHECK( (w > 0) && (h > 0) );
    LOG_CHECK( (zfar - znear) > 0.0f );

    fov_    = fov;
    width_  = w;
    height_ = h;

    // Projection matrix.
    auto const ratio = static_cast<float>(width_) / static_cast<float>(height_);
    set_projection(linalg::perspective_matrix(
      fov_, ratio, znear, zfar, linalg::neg_z, linalg::zero_to_one
    ));

    // Linearization parameters.
    float const A = zfar / (zfar - znear);
    linear_params_ = vec4( znear, zfar, A, - znear * A);
    linear_params_set_ = true;
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
    LOG_CHECK(view_count() <= transforms_.size());
    {
      auto const view_id = 0u;
      auto &T = transforms_[view_id];
      if (controller_ && bRetrieveView) {
        controller_->calculateViewMatrix(&T.view, view_id);
      }
      T.world = linalg::inverse(T.view);
      T.view_projection = linalg::mul(T.projection, T.view);
    }
    if (view_count() == 2u)
    {
      auto const view_id = 1u;
      auto &T = transforms_[view_id];
      if (controller_ && bRetrieveView) {
        controller_->calculateViewMatrix(&T.view, view_id);
      }
      T.world = linalg::inverse(T.view);
      T.view_projection = linalg::mul(T.projection, T.view);
    }
    need_rebuild_ = false;
    rebuilt_ = true;
  }

  /* --- Setters --- */

  void set_controller(ViewController *controller) noexcept {
    controller_ = controller;
    need_rebuild_ = true;
  }

  void set_projection(mat4 const& projection, uint32_t view_id = 0u) {
    LOG_CHECK(view_id < transforms_.size());
    auto &T = transforms_[view_id];
    T.projection = projection;
    T.projection_inverse = linalg::inverse(T.projection);
    need_rebuild_ = true;
    linear_params_set_ = false;
  }

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
  uint32_t view_count() const noexcept {
    return controller_ ? controller_->view_count() : 1u;
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
  vec4 const& linearization_params() const {
    LOG_CHECK(linear_params_set_);
    return linear_params_;
  }

  // -------------------------------

  [[nodiscard]]
  Transform const& transform(uint32_t view_id = 0u) const {
    LOG_CHECK(view_id < transforms_.size());
    return transforms_[view_id];
  }

  [[nodiscard]]
  auto const& transforms() const noexcept {
    return transforms_;
  }

  // -------------------------------

  [[nodiscard]]
  mat4 const& proj(uint32_t view_id = 0u) const {
    return transforms_[view_id].projection;
  }

  // [[nodiscard]]
  // mat4 const& proj_inverse() const noexcept {
  //   return transform_.projection_inverse;
  // }

  [[nodiscard]]
  mat4 const& view(uint32_t view_id = 0u) const {
    return transforms_[view_id].view;
  }

  // [[nodiscard]]
  // mat4 const& world() const noexcept {
  //   return transform_.world;
  // }

  // [[nodiscard]]
  // mat4 const& viewproj() const noexcept {
  //   return transform_.view_projection;
  // }

  // -------------------------------

  [[nodiscard]]
  vec3 position(uint32_t view_id = 0u) const {
    return transform(view_id).position(); // XXX
  }

  [[nodiscard]]
  vec3 direction(uint32_t view_id = 0u) const {
    return transform(view_id).direction(); // XXX
  }

  [[nodiscard]]
  vec3 target(uint32_t view_id = 0u) const {
    return controller_ ? controller_->target()
                       : position(view_id) + 3.0f * direction(view_id) //
                       ;
  }

  // -------------------------------

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

  // Transform transform_{};
  std::array<Transform, 2u> transforms_{};

  bool need_rebuild_{true};
  bool rebuilt_{false};
  bool linear_params_set_{false};
};

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_CAMERA_H_
