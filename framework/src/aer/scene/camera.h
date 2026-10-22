#ifndef AER_SCENE_CAMERA_H_
#define AER_SCENE_CAMERA_H_

#include "aer/core/common.h"
#include "aer/core/view_controller_interface.h"

#include "aer/scene/frustum.h"
#include "aer/scene/ray.h"

/* -------------------------------------------------------------------------- */

//
// Camera using right-handed, -Z forward, column-major layout
//
class Camera {
 public:
  static constexpr float kDefaultFOV = lina::radians(60.0f);
  static constexpr float kDefaultNear = 0.1f;
  static constexpr float kDefaultFar = 500.0f;
  static constexpr uint32_t kDefaultSize = 512u;

  struct Transform {
    mat4 projection{lina::identity};
    mat4 projection_inverse{lina::identity};
    mat4 view{lina::translation_matrix(vec3(0.0f, 0.0f, -1.0f))};
    mat4 world{lina::identity}; // view_inverse
    mat4 view_projection{lina::identity};

    [[nodiscard]]
    vec3 position() const noexcept {
      return lina::to_vec3(world[3]); //
    }

    [[nodiscard]]
    vec3 direction() const {
      return lina::normalize(-lina::to_vec3(world[2])); //
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

  Camera(ViewControllerInterface *controller)
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
    set_projection(lina::perspective_matrix(
      fov_, ratio, znear, zfar, lina::neg_z, lina::zero_to_one
    ));

    // Linearization parameters.
    float const A = zfar / (zfar - znear);
    linear_params_ = vec4( znear, zfar, A, - znear * A);
    linear_params_set_ = true;
  }

  void makePerspective(float fov, uvec2 const& resolution, float znear, float zfar) {
    makePerspective(fov, resolution.x, resolution.y, znear, zfar);
  }

  /* Create a default perspective projection camera. */
  void makeDefault() {
    makePerspective(
      kDefaultFOV, kDefaultSize, kDefaultSize, kDefaultNear, kDefaultFar
    );
  }

  void makeDefault(uvec2 const& resolution) {
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
      T.world = lina::inverse(T.view);
      T.view_projection = lina::mul(T.projection, T.view);
    }
    if (view_count() == 2u)
    {
      auto const view_id = 1u;
      auto &T = transforms_[view_id];
      if (controller_ && bRetrieveView) {
        controller_->calculateViewMatrix(&T.view, view_id);
      }
      T.world = lina::inverse(T.view);
      T.view_projection = lina::mul(T.projection, T.view);
    }
    need_rebuild_ = false;
    rebuilt_ = true;
  }

  /* --- Setters --- */

  void set_controller(ViewControllerInterface *controller) noexcept {
    controller_ = controller;
    need_rebuild_ = true;
  }

  void set_projection(mat4 const& projection, uint32_t view_id = 0u) {
    LOG_CHECK(view_id < transforms_.size());
    auto &T = transforms_[view_id];
    T.projection = projection;
    T.projection_inverse = lina::inverse(T.projection);
    need_rebuild_ = true;
    linear_params_set_ = false;
  }

  /* --- Getters --- */

  [[nodiscard]]
  ViewControllerInterface* controller() noexcept {
    return controller_;
  }

  [[nodiscard]]
  ViewControllerInterface const* controller() const noexcept {
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
  //   return transforms_[view_id].world;
  // }

  [[nodiscard]]
  mat4 const& viewproj(uint32_t view_id = 0u) const noexcept {
    return transforms_[view_id].view_projection;
  }

  [[nodiscard]]
  Frustum frustum(uint32_t view_id = 0u) const {
    return Frustum::FromViewProj(viewproj(view_id));
  }

  [[nodiscard]]
  scene::Ray screenToWorldRay(vec2 const& mouse_pos, uint32_t view_id = 0u) const {
    auto const& T = transform(view_id);

    // 1. Convert screen coordinates (in pixels)
    //  to Normalized Device Coordinates (NDC) ranging from [-1, 1].
    float const x = (2.0f * mouse_pos.x / static_cast<float>(width_)) - 1.0f;
    float const y = 1.0f - (2.0f * mouse_pos.y / static_cast<float>(height_)); //

    // 2. Near and Far points in NDC space (Vulkan / zero_to_one depth convention).
    vec4 const ray_clip_near(x, y, 0.0f, 1.0f);
    vec4 const ray_clip_far (x, y, 1.0f, 1.0f);

    // 3. Unproject from clip space to eye (view) space.
    vec4 ray_eye_near = lina::mul(T.projection_inverse, ray_clip_near);
    vec4 ray_eye_far  = lina::mul(T.projection_inverse, ray_clip_far);
    ray_eye_near = ray_eye_near / ray_eye_near.w;
    ray_eye_far  = ray_eye_far / ray_eye_far.w;

    // 4. Transform from eye space to world space using the camera's world matrix
    vec4 const ray_world_near = lina::mul(T.world, ray_eye_near);
    vec4 const ray_world_far  = lina::mul(T.world, ray_eye_far);

    vec3 const origin = lina::to_vec3(ray_world_near);
    vec3 const direction = lina::normalize(lina::to_vec3(ray_world_far) - origin);

    return scene::Ray{ origin, direction };
  }

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

  [[nodiscard]]
  vec2 tan_fovs() const {
    auto const tan_half_fovy = std::tan(fov() * 0.5f);
    return {
      aspect() * tan_half_fovy,
      tan_half_fovy
    };
  }

  [[nodiscard]]
  vec2 focals() const {
    auto const tf = tan_fovs();
    return {
      0.5f * static_cast<float>(width_) / tf.x,
      0.5f * static_cast<float>(height_) / tf.y
    };
  }

  // -------------------------------

  [[nodiscard]]
  bool rebuilt() const noexcept {
    return rebuilt_;
  }

 private:
  ViewControllerInterface *controller_{};

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

#endif // AER_SCENE_CAMERA_H_
