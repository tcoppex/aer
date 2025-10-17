#ifndef AER_CORE_PLATFORM_OPENXR_CONTEXT_H_
#define AER_CORE_PLATFORM_OPENXR_CONTEXT_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/core/camera.h"

#include "aer/platform/openxr/xr_common.h"
#include "aer/platform/openxr/xr_utils.h"
#include "aer/platform/openxr/xr_platform_interface.h"
#include "aer/platform/openxr/xr_vulkan_interface.h" //
#include "aer/platform/openxr/xr_swapchain.h"

/* -------------------------------------------------------------------------- */

class OpenXRContext {
 private:
  // Generic parameters for a stereoscopic VR Headset.
  static constexpr XrFormFactor kHMDFormFactor{
    XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY
  };
  static constexpr XrViewConfigurationType kViewConfigurationType{
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO
  };
  static constexpr uint32_t kNumEyes{2u};
  static constexpr uint32_t kMaxNumCompositionLayers{16u};

 public:
  union CompositorLayerUnion_t {
    XrCompositionLayerProjection projection;
    XrCompositionLayerQuad quad;
    XrCompositionLayerCylinderKHR cylinder;
    XrCompositionLayerCubeKHR cube;
    XrCompositionLayerEquirectKHR equirect;
    XrCompositionLayerPassthroughFB passthrough;
  };

  template<typename T> using XRStereoBuffer = std::array<T, kNumEyes>;

 public:
  // -- Public XR Interface --

  virtual ~OpenXRContext() = default;

  [[nodiscard]]
  bool init(
    XRPlatformInterface const& platform,
    std::string_view appName,
    std::vector<char const*> const& appExtensions
  );

  [[nodiscard]]
  bool initSession();

  [[nodiscard]]
  bool resetSwapchain();

  [[nodiscard]]
  bool completeSetup(Camera &camera);

  void shutdown();

  void pollEvents();

  void processFrame(
    XRUpdateFunc_t const& update_frame_cb,
    XRRenderFunc_t const& render_view_cb
  );

  [[nodiscard]]
  bool isSessionRunning() const noexcept {
    return session_running_;
  }

  [[nodiscard]]
  bool isSessionFocused() const noexcept {
    return session_focused_;
  }

  [[nodiscard]]
  bool shouldStopRender() const noexcept {
    return end_render_loop_;
  }


  // -- Public instance getters / helpers --

  [[nodiscard]]
  XrSpace base_space() const {
    return spaces_[base_space_index_];
  }

  [[nodiscard]]
  xrutils::SpaceLocationVelocity_t spaceLocationVelocity(
    XrSpace space,
    XrTime time
  ) const {
    return xrutils::SpaceLocationVelocity(space, base_space(), time);
  }

  [[nodiscard]]
  XrSpaceLocation spaceLocation(
    XrSpace space,
    XrTime time
  ) const {
    return spaceLocationVelocity(space, time).first;
  }

  [[nodiscard]]
  XrSpaceVelocity spaceVelocity(
    XrSpace space,
    XrTime time
  ) const {
    return spaceLocationVelocity(space, time).second;
  }

  [[nodiscard]]
  XRVulkanInterface* graphics_interface() noexcept {
    return graphics_.get(); //
  }

  [[nodiscard]]
  SwapchainInterface* swapchain_interface() noexcept {
    return &swapchain_;
  }

  [[nodiscard]]
  XRFrameData const& frame_data() const noexcept {
    return frame_data_;
  }

  [[nodiscard]]
  XRControlState_t::Frame const& frame_control_state() const noexcept {
    return controls_.frame;
  }

 private:
  [[nodiscard]]
  bool initControllers();

  void createReferenceSpaces();

  void handleSessionStateChangedEvent(
    XrEventDataSessionStateChanged const& sessionStateChanged
  );

  void beginFrame();

  void endFrame();

  void handleControls();

  void renderProjectionLayer(XRRenderFunc_t const& render_view_cb);

 protected:
  XrInstance instance_{XR_NULL_HANDLE};
  XrSystemId system_id_{XR_NULL_SYSTEM_ID};

  std::unique_ptr<XRVulkanInterface> graphics_{}; //

  XrSession session_{XR_NULL_HANDLE};

 private:
  XrEventDataBuffer event_data_buffer_{};

  bool session_running_ = false;
  bool session_focused_ = false;
  bool request_restart_ = false;
  bool end_render_loop_ = false;

  // -----

  uint32_t base_space_index_{XRSpaceId::Head};
  std::array<XrSpace, XRSpaceId::kNumSpaceId> spaces_{};
  std::array<mat4f, XRSpaceId::kNumSpaceId> spaceMatrices_{};

  // -----

  XRStereoBuffer<XrViewConfigurationView> view_config_views_{};
  XRStereoBuffer<XrView> views_{};

  OpenXRSwapchain swapchain_{}; // color swapchain

  XRStereoBuffer<XrCompositionLayerProjectionView> layer_projection_views_{};
  std::array<CompositorLayerUnion_t, kMaxNumCompositionLayers> layers_{};
  uint32_t num_layers_{};

  std::vector<XrCompositionLayerBaseHeader const*> composition_layers_{};

  // -----

  XRControlState_t controls_{};
  XRFrameData frame_data_{}; //

  Camera *camera_ptr_{};

  bool should_render_{};

  struct ViewController final : Camera::ViewController {
    ViewController(XRFrameData const& frame_data)
      : frame_data_{frame_data}
    {}
    virtual ~ViewController() = default;

    bool update(float dt) final {
      return true;
    }

    void calculateViewMatrix(mat4 *view_matrix, uint32_t view_id) final {
      *view_matrix = frame_data_.viewMatrices[view_id];
    }

    uint32_t view_count() const noexcept final {
      return 2u;
    }

    vec3 target() const final {
      LOGW("OpenXR::ViewController::target not implemented");
      return vec3(); //
    }

   private:
    XRFrameData const& frame_data_;
  } view_controller_{frame_data_};
};

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_PLATFORM_OPENXR_CONTEXT_H_
