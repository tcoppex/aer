#include <cstring>
#include <cctype>
#include <algorithm>
#include <string>
#include <unordered_set>

#include "aer/platform/openxr/openxr_context.h"
#include "aer/platform/openxr/xr_utils.h"

/* -------------------------------------------------------------------------- */

#if !defined(FRAMEWORK_NAME)
#define FRAMEWORK_NAME "aer" //
#endif

static constexpr char const* kEngineName{FRAMEWORK_NAME}; //

/* -------------------------------------------------------------------------- */

namespace {

/* Localized an input parameter name : "new_param" to "New Param". */
std::string LocalizeString(std::string s) {
  bool capitalizeNext = true;
  for (char& c : s) {
    if (c != '_') [[likely]] {
      c = capitalizeNext ? std::toupper(c) : std::tolower(c);
      capitalizeNext = std::isspace(c);
    } else {
      c = ' ';
      capitalizeNext = true;
    }
  }
  return s;
}

void CopyStringWithSafety(char *const dst, std::string const& src, size_t size) {
  LOG_CHECK(size > 0);
  strncpy(dst, src.c_str(), size - 1);
  dst[size - 1] = '\0';
}

void SetActionNames(
  char dst[XR_MAX_ACTION_NAME_SIZE], 
  char dst_localized[XR_MAX_LOCALIZED_ACTION_NAME_SIZE],
  std::string const& src
) {
  CopyStringWithSafety(dst, src, XR_MAX_ACTION_NAME_SIZE - 1);
  CopyStringWithSafety(dst_localized, LocalizeString(src), XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
}

XrPosef PoseIdentity() {
  return {.orientation = {.w = 1.0f}};
}

} // namespace

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

bool OpenXRContext::init(
  XRPlatformInterface const& platform,
  std::string_view appName,
  std::vector<char const*> const& appExtensions
) {
  /* Initialize the loader when available. */
  {
    PFN_xrInitializeLoaderKHR initializeLoader{nullptr};
    CHECK_XR_RET(xrGetInstanceProcAddr(
      XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)(&initializeLoader)
    ))
    if (auto info = platform.loaderInitInfo(); nullptr != info) [[likely]] {
      initializeLoader(info);
    }
  }

  /* Create the Instance. */
  {
    LOG_CHECK(XR_NULL_HANDLE == instance_);

    std::vector<char const*> api_layers{};
    std::vector<char const*> extensions{};

    // Merge Platform's, Graphics API's and user's XR extensions into an unique array.
    std::unordered_set<std::string_view> unique_exts{
      /// Vulkan Graphics API
      XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,

      /// Default Composition Layer
      XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME,
      // XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME,
      // XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME,
    };
    auto const& platform_ext = platform.instanceExtensions();
    unique_exts.insert(platform_ext.cbegin(), platform_ext.cend());
    unique_exts.insert(appExtensions.cbegin(), appExtensions.cend());

    // [it's safe to push string_view ptr into extensions because we don't keep
    //  the data post unique_exts lifetime].
    extensions.reserve(unique_exts.size());
    for (auto const& ext : unique_exts) {
      extensions.push_back(ext.data());
    }

    XrInstanceCreateInfo info{
      .type = XR_TYPE_INSTANCE_CREATE_INFO,
      .next = platform.instanceCreateInfo(),
      .applicationInfo = {
        .applicationVersion = 1u, //
        .engineVersion = 1u, //
        .apiVersion = XR_API_VERSION_1_1, //XR_MAKE_VERSION(1, 0, 0),
      },
      .enabledApiLayerCount = static_cast<uint32_t>(api_layers.size()),
      .enabledApiLayerNames = api_layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
      .enabledExtensionNames = extensions.data(),
    };

    CopyStringWithSafety(
      info.applicationInfo.applicationName, appName.data(), XR_MAX_APPLICATION_NAME_SIZE
    );
    CopyStringWithSafety(
      info.applicationInfo.engineName, kEngineName, XR_MAX_ENGINE_NAME_SIZE
    );

    CHECK_XR_RET(xrCreateInstance(&info, &instance_))
  }

  /* System. */
  {
    LOG_CHECK(XR_NULL_HANDLE != instance_);
    LOG_CHECK(XR_NULL_SYSTEM_ID == system_id_);
    XrSystemGetInfo system_info{
      .type = XR_TYPE_SYSTEM_GET_INFO,
      .formFactor = kHMDFormFactor,
    };
    CHECK_XR_RET(xrGetSystem(instance_, &system_info, &system_id_))
  }

  graphics_ = std::make_unique<XRVulkanInterface>(instance_, system_id_);

  return true;
}

// ----------------------------------------------------------------------------

bool OpenXRContext::initSession() {
  LOG_CHECK(graphics_ != nullptr);
  LOG_CHECK(XR_NULL_HANDLE != instance_);
  LOG_CHECK(XR_NULL_SYSTEM_ID != system_id_);
  LOG_CHECK(XR_NULL_HANDLE == session_);

  XrSessionCreateInfo const create_info{
    .type = XR_TYPE_SESSION_CREATE_INFO,
    .next = graphics_->binding(),
    .createFlags = 0,
    .systemId = system_id_,
  };
  CHECK_XR_RET(xrCreateSession(instance_, &create_info, &session_))

  return true;
}

// ----------------------------------------------------------------------------

bool OpenXRContext::resetSwapchain() {
  LOG_CHECK(XR_NULL_HANDLE != instance_);
  LOG_CHECK(XR_NULL_HANDLE != session_);

  if (swapchain_.handle() != VK_NULL_HANDLE) {
    swapchain_.destroy();
  }

  /// NOTE:
  /// We need to use one swapchain per view, each of this swapchain
  /// will use N-in flights images and will require their own
  /// vulkan command buffer / render target.

  XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
  CHECK_XR_RET(xrGetSystemProperties(instance_, system_id_, &systemProperties))

  /* Retrieve ViewConfiguration views. */
  {
    uint32_t viewCount(0u);
    CHECK_XR(xrEnumerateViewConfigurationViews(
      instance_, system_id_, kViewConfigurationType, 0, &viewCount, nullptr
    ));
    LOG_CHECK(kNumEyes == viewCount); //

    view_config_views_.fill({XR_TYPE_VIEW_CONFIGURATION_VIEW});
    views_.fill({XR_TYPE_VIEW});

    CHECK_XR_RET(xrEnumerateViewConfigurationViews(
      instance_,
      system_id_,
      kViewConfigurationType,
      viewCount,
      &viewCount,
      view_config_views_.data()
    ))
  }

  /* Retrieve swapchain formats. */
  uint32_t formatCount(0u);
  CHECK_XR(xrEnumerateSwapchainFormats(session_, 0, &formatCount, nullptr));
  std::vector<int64_t> formats(formatCount);
  CHECK_XR_RET(xrEnumerateSwapchainFormats(
    session_, formatCount, &formatCount, formats.data()
  ))

  /* Create the main color swapchain. */
  {
    auto& config_view{ view_config_views_[0] };

    // A multiview swapchain image is a 2D array image with 2 layers (left eye, right eye).
    XrSwapchainCreateInfo create_info{
      .type         = XR_TYPE_SWAPCHAIN_CREATE_INFO,
      .createFlags  = 0,
      .usageFlags   = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT
                    | XR_SWAPCHAIN_USAGE_SAMPLED_BIT            //
                    | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT       // to blit on it
                    | XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT
                    | XR_SWAPCHAIN_USAGE_UNORDERED_ACCESS_BIT
                    ,
      .format       = graphics_->selectColorSwapchainFormat(formats),
      .sampleCount  = config_view.recommendedSwapchainSampleCount,
      .width        = config_view.recommendedImageRectWidth,
      .height       = config_view.recommendedImageRectHeight,
      .faceCount    = 1,
      .arraySize    = kNumEyes,
      .mipCount     = 1,
    };

    if (!swapchain_.create(session_, create_info, graphics_.get())) {
      return false;
    }
  }

  // [TODO]
  // we need a depth swapchain only to alter the perception of depth
  // for specific XR techniques.

  return true;
}

// ----------------------------------------------------------------------------

bool OpenXRContext::completeSetup() {
  LOG_CHECK(XR_NULL_HANDLE != session_);
  LOG_CHECK(XR_NULL_HANDLE != swapchain_.handle());

  // Default controllers.
  if (!initControllers()) [[unlikely]] {
    LOGE("OpenXRContext: controller initialization failed.");
    return false;
  }

  // Reference Spaces.
  createReferenceSpaces();

  return true;
}

// ----------------------------------------------------------------------------

void OpenXRContext::shutdown() {
  if (instance_ == XR_NULL_HANDLE) {
    return;
  }

  if (controls_.action_set != XR_NULL_HANDLE) {
    for (auto side : {XRSide::Left, XRSide::Right}) {
      xrDestroySpace(controls_.hand.aim_space[side]);
      xrDestroySpace(controls_.hand.grip_space[side]);
    }
    xrDestroyActionSet(controls_.action_set);
    controls_.action_set = XR_NULL_HANDLE;
  }

  swapchain_.destroy();

  for (auto &space : spaces_) {
    xrDestroySpace(space);
  }
  if (session_ != XR_NULL_HANDLE) {
    xrDestroySession(session_);
    session_ = XR_NULL_HANDLE;
  }

  graphics_.reset();

  xrDestroyInstance(instance_);
  instance_ = XR_NULL_HANDLE;
}

// ----------------------------------------------------------------------------

void OpenXRContext::pollEvents() {
  while (true) {
    // Clear event data buffer.
    auto* event{reinterpret_cast<XrEventDataBaseHeader*>(&event_data_buffer_)};
    *event = { XR_TYPE_EVENT_DATA_BUFFER };

    // Retrieve event if any.
    if (auto res = xrPollEvent(instance_, &event_data_buffer_); XR_EVENT_UNAVAILABLE == res) {
      break;
    }

    // Handle event.
    switch (event->type) {
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
      {
        auto const& instanceLossPending{
          *reinterpret_cast<XrEventDataInstanceLossPending const*>(event)
        };
        LOGW("XrEventDataInstanceLossPending by {}.", instanceLossPending.lossTime);
        end_render_loop_ = true;
        request_restart_ = true;
      }
      break;

      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
      {
        LOGD("OpenXR: event XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED");
        auto const& sessionStateChanged{
          *reinterpret_cast<XrEventDataSessionStateChanged const*>(event)
        };
        handleSessionStateChangedEvent(sessionStateChanged);
      }
      break;

      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
        LOGD("OpenXR: event XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED");
      break;

      case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
        LOGD("OpenXR: event XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING");
      break;

      default:
        LOGD("OpenXR: Ignored type event {}", (int)event->type);
      break;
    }
  }
}

// ----------------------------------------------------------------------------

void OpenXRContext::beginFrame() {
  // LOGD("-- OpenXRContext::beginFrame -- ");
  auto& frameState = controls_.frame.state;

  shouldRender_ = false;

  // -- Wait Frame.
  {
    frameState = { XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
    CHECK_XR(xrWaitFrame(session_, &frameWaitInfo, &frameState));
  }

  // -- Sync actions.
  {
    XrActiveActionSet const activeActionSet{controls_.action_set, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{
      .type = XR_TYPE_ACTIONS_SYNC_INFO,
      .countActiveActionSets = 1u,
      .activeActionSets = &activeActionSet,
    };
    CHECK_XR(xrSyncActions(session_, &syncInfo));
  }

  handleControls(); //

  // -- Begin Frame.
  {
    XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    CHECK_XR(xrBeginFrame(session_, &frameBeginInfo));
  }

  // -- Retrieve Views infos.
  {
    XrViewState viewState{XR_TYPE_VIEW_STATE};

    XrViewLocateInfo viewLocateInfo{
      .type = XR_TYPE_VIEW_LOCATE_INFO,
      .viewConfigurationType = kViewConfigurationType,
      .displayTime = frameState.predictedDisplayTime,
      .space = base_space(), //
    };
    uint32_t const viewCapacityInput{static_cast<uint32_t>(views_.size())};
    uint32_t viewCountOutput{};
    CHECK_XR(xrLocateViews(
      session_,
      &viewLocateInfo,
      &viewState,
      viewCapacityInput,
      &viewCountOutput,
      views_.data()
    ));

    // Check our buffers are correctly sized.
    LOG_CHECK(viewCountOutput == viewCapacityInput);
    LOG_CHECK(viewCountOutput == view_config_views_.size());
    LOG_CHECK(viewCountOutput == kNumEyes);

    // Check the views have tracking poses.
    shouldRender_ = (XR_TRUE == frameState.shouldRender)
                 && (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)
                 && (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)
                  ;
  }
}

// ----------------------------------------------------------------------------

void OpenXRContext::endFrame() {
  // LOGD("-- OpenXRContext::endFrame -- ");
  auto& frameState = controls_.frame.state;

  XrFrameEndInfo frameEndInfo{
    .type = XR_TYPE_FRAME_END_INFO,
    .displayTime = frameState.predictedDisplayTime,
    .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE, // << App Specific
    .layerCount = num_layers_,
    .layers = composition_layers_.data(),
  };
  end_render_loop_ = CHECK_XR(xrEndFrame(session_, &frameEndInfo)) < 0;
  composition_layers_.clear();
}

// ----------------------------------------------------------------------------

void OpenXRContext::processFrame(
  XRUpdateFunc_t const& update_cb,
  XRRenderFunc_t const& render_cb
) {
  LOG_CHECK(XR_NULL_HANDLE != session_);

  beginFrame();
  {
    // shouldRender_ might be false here, but we still call update.

    // UPDATE.
    {
      auto& frameState = controls_.frame.state;
      float const nearZ = 0.05f; //
      float const farZ = 150.0f; //

      // Calculate space matrices.
      for (uint32_t i = 0u; i < spaces_.size(); ++i) {
        auto const spaceLoc = spaceLocation(spaces_[i], frameState.predictedDisplayTime);
        if (xrutils::IsSpaceLocationValid(spaceLoc)) {
          spaceMatrices_[i] = xrutils::PoseMatrix(spaceLoc.pose);
          frameData_.spaceMatrices[i] = &spaceMatrices_[i];
        } else {
          frameData_.spaceMatrices[i] = nullptr;
        }
      }

      // Calculate transforms for each view.
      for (uint32_t i = 0u; i < kNumEyes; ++i) {
        auto const &view = views_[i];
        auto const pose{xrutils::PoseMatrix(view.pose)};
        frameData_.viewMatrices[i] = lina::rigidbody_inverse(pose); //
        frameData_.projMatrices[i] = xrutils::ProjectionMatrix(view.fov, nearZ, farZ);
      }
      // frameData_.headMatrix = xrutils::PoseMatrix(controls_.frame.head_pose);
      // frameData_.predictedDisplayTime = 1.0e9 * static_cast<double>(frameState.predictedDisplayTime); //
      frameData_.shouldRender = shouldRender_;

      update_cb(/*frameData_*/);
    }

    // RENDER.
    {
      num_layers_ = 0u;
      layers_.fill(CompositorLayerUnion_t{});

      // -------------------------------------------------
      if (shouldRender_) {
        renderProjectionLayer(render_cb);

        // [TODO: add system to handle user specific layers]
        // Add the world view projection layer to the composition.
        layers_[num_layers_++].projection = XrCompositionLayerProjection{
          .type       = XR_TYPE_COMPOSITION_LAYER_PROJECTION,
          .layerFlags = 0
                      | XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT
                      | XR_COMPOSITION_LAYER_CORRECT_CHROMATIC_ABERRATION_BIT
                      | XR_COMPOSITION_LAYER_UNPREMULTIPLIED_ALPHA_BIT
                      ,
          .space      = base_space(), //
          .viewCount  = kNumEyes,
          .views      = layer_projection_views_.data()
        };
      } else {
        LOGV("shouldRender_ is set to false");
      }
      // -------------------------------------------------

      for (auto & layer : layers_) {
        composition_layers_.push_back(
          reinterpret_cast<XrCompositionLayerBaseHeader const*>(&layer)
        );
      }
    }
  }
  endFrame();
}

// -----------------------------------------------------------------------------

void OpenXRContext::renderProjectionLayer(XRRenderFunc_t const& render_cb) {
  // LOGD(">>> OpenXRContext::renderProjectionLayer ");

  // The swapchain image is currently acquired and released inside the render cb.
  render_cb();

  /* Setup projection layers for each Eyes. */
  for (uint32_t view_id = 0u; view_id < kNumEyes; ++view_id) {
    auto const& view{ views_[view_id] };
    layer_projection_views_[view_id] = XrCompositionLayerProjectionView{
      .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
      .pose = view.pose,
      .fov = view.fov,
      .subImage = {
        .swapchain = swapchain_.handle(),
        .imageRect = swapchain_.rect(),
        .imageArrayIndex = view_id,
      }
    };
  }
}

/* -------------------------------------------------------------------------- */

bool OpenXRContext::initControllers() {
  LOG_CHECK(XR_NULL_HANDLE != session_);

  // Shortcut to Oculus Touch like actions & hand sideness params.
  auto& touch{ controls_.action };
  auto& hand{ controls_.hand };

  // -- Create Action Set.
  {
    XrActionSetCreateInfo create_info{
      .type = XR_TYPE_ACTION_SET_CREATE_INFO,
      .priority = 1u,
    };
    SetActionNames(create_info.actionSetName, create_info.localizedActionSetName, "gameplay");
    CHECK_XR_RET(xrCreateActionSet(instance_, &create_info, &controls_.action_set))
  }

  // Get the XrPath for the left and right hands.
  CHECK_XR_RET(xrStringToPath(instance_, "/user/hand/left", &hand.path[XRSide::Left]))
  CHECK_XR_RET(xrStringToPath(instance_, "/user/hand/right", &hand.path[XRSide::Right]))

  // -- Create Actions.
  {
    auto createAction{
      [&](XrAction &action,
          XrActionType actionType,
          std::string const& name,
          bool both_side = true
        ) {
        XrActionCreateInfo actionInfo{
          .type = XR_TYPE_ACTION_CREATE_INFO,
          .next = nullptr,
          .actionType = actionType,
          .countSubactionPaths = both_side ? static_cast<uint32_t>(XRSide::kNumSide) : 0u,
          .subactionPaths = both_side ? hand.path.data() : nullptr,
        };
        SetActionNames(actionInfo.actionName, actionInfo.localizedActionName, name);
        CHECK_XR(xrCreateAction(controls_.action_set, &actionInfo, &action));
      }
    };

    // (Note : We use the Oculus Touch controller as default template)

    // Dual inputs.
    createAction(touch.aim_pose,      XR_ACTION_TYPE_POSE_INPUT,        "aim_pose");
    createAction(touch.grip_pose,     XR_ACTION_TYPE_POSE_INPUT,        "grip_pose");
    createAction(touch.index_trigger, XR_ACTION_TYPE_FLOAT_INPUT,       "index_trigger");
    createAction(touch.grip_squeeze,  XR_ACTION_TYPE_FLOAT_INPUT,       "grip_squeeze");
    createAction(touch.joystick,      XR_ACTION_TYPE_VECTOR2F_INPUT,    "thumbstick");
    createAction(touch.click_joystick,XR_ACTION_TYPE_BOOLEAN_INPUT,     "click_thumbstick");
    createAction(touch.touch_trigger, XR_ACTION_TYPE_BOOLEAN_INPUT,     "touch_trigger");
    createAction(touch.touch_joystick,XR_ACTION_TYPE_BOOLEAN_INPUT,     "touch_stick");
    // Sided inputs.
    createAction(touch.button_x,      XR_ACTION_TYPE_BOOLEAN_INPUT,     "button_x", false);
    createAction(touch.button_y,      XR_ACTION_TYPE_BOOLEAN_INPUT,     "button_y", false);
    createAction(touch.button_menu,   XR_ACTION_TYPE_BOOLEAN_INPUT,     "button_menu", false);
    createAction(touch.button_a,      XR_ACTION_TYPE_BOOLEAN_INPUT,     "button_a", false);
    createAction(touch.button_b,      XR_ACTION_TYPE_BOOLEAN_INPUT,     "button_b", false);
    createAction(touch.button_system, XR_ACTION_TYPE_BOOLEAN_INPUT,     "button_system", false);
    createAction(touch.touch_x,       XR_ACTION_TYPE_BOOLEAN_INPUT,     "touch_x", false);
    createAction(touch.touch_y,       XR_ACTION_TYPE_BOOLEAN_INPUT,     "touch_y", false);
    createAction(touch.touch_a,       XR_ACTION_TYPE_BOOLEAN_INPUT,     "touch_a", false);
    createAction(touch.touch_b,       XR_ACTION_TYPE_BOOLEAN_INPUT,     "touch_b", false);
    // Dual output.
    createAction(touch.vibrate,       XR_ACTION_TYPE_VIBRATION_OUTPUT,  "vibrate_hand");
  }

  // -- Suggest Interaction Profile Bindings.
  {
    using SuggestedBindings_t = std::vector<XrActionSuggestedBinding>;

    auto suggestInteractionProfileBindings{
      [&](std::string const& profile_path_str, SuggestedBindings_t && bindings) {
        XrPath profile = XR_NULL_PATH;
        CHECK_XR(xrStringToPath(instance_, profile_path_str.data(), &profile));
        XrInteractionProfileSuggestedBinding profile_bindings{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        profile_bindings.interactionProfile = profile;
        profile_bindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        profile_bindings.suggestedBindings = bindings.data();
        CHECK_XR(xrSuggestInteractionProfileBindings(instance_, &profile_bindings));
      }
    };

    // [lambda] Helpers to suggest bind path to actions.
    auto bindingPath{
      [&](XRSide side, std::string const& path_suffix, bool is_input) {
        auto const path_str = std::string()
                            + "/user/hand"
                            + ((side == XRSide::Left) ? "/left" : "/right")
                            + (is_input ? "/input" : "/output")
                            + path_suffix
                            ;
        XrPath binding;
        CHECK_XR(xrStringToPath(instance_, path_str.data(), &binding));
        return binding;
      }
    };

    auto left{
      [&](XrAction action, auto s, bool is_input = true) {
        return XrActionSuggestedBinding{
          .action = action,
          .binding = bindingPath(XRSide::Left, s, is_input),
        };
      }
    };

    auto right{
      [&](XrAction action, auto s, bool is_input = true) {
        return XrActionSuggestedBinding{
          .action = action,
          .binding = bindingPath(XRSide::Right, s, is_input),
        };
      }
    };

    // Oculus Touch.
    suggestInteractionProfileBindings(
      "/interaction_profiles/oculus/touch_controller",
      {
        left(touch.aim_pose,        "/aim/pose"),
        left(touch.grip_pose,       "/grip/pose"),
        left(touch.grip_squeeze,    "/squeeze/value"),
        left(touch.index_trigger,   "/trigger/value"),
        left(touch.touch_trigger,   "/trigger/touch"),
        left(touch.joystick,        "/thumbstick"),
        left(touch.touch_joystick,  "/thumbstick/touch"),
        left(touch.click_joystick,  "/thumbstick/click"),
        left(touch.button_x,        "/x/click"),
        left(touch.touch_x,         "/x/touch"),
        left(touch.button_y,        "/y/click"),
        left(touch.touch_y,         "/y/touch"),
        left(touch.button_menu,     "/menu/click"),
        left(touch.vibrate,         "/haptic", false),

        right(touch.aim_pose,       "/aim/pose"),
        right(touch.grip_pose,      "/grip/pose"),
        right(touch.grip_squeeze,   "/squeeze/value"),
        right(touch.index_trigger,  "/trigger/value"),
        right(touch.touch_trigger,  "/trigger/touch"),
        right(touch.joystick,       "/thumbstick"),
        right(touch.touch_joystick, "/thumbstick/touch"),
        right(touch.click_joystick, "/thumbstick/click"),
        right(touch.button_a,       "/a/click"),
        right(touch.touch_a,        "/a/touch"),
        right(touch.button_b,       "/b/click"),
        right(touch.touch_b,        "/b/touch"),
        right(touch.button_system,  "/system/click"),
        right(touch.vibrate,        "/haptic", false),
      }
    );

    // Khronos default controller.
    suggestInteractionProfileBindings(
      "/interaction_profiles/khr/simple_controller",
      {
        left(touch.grip_squeeze,  "/select/click"),
        left(touch.grip_pose,     "/grip/pose"),
        left(touch.button_menu,   "/menu/click"),
        left(touch.vibrate,       "/haptic", false),

        right(touch.grip_squeeze, "/select/click"),
        right(touch.grip_pose,    "/grip/pose"),
        right(touch.button_system,"/menu/click"), //
        right(touch.vibrate,      "/haptic", false),
      }
    );

    // Vive controller.
    suggestInteractionProfileBindings(
      "/interaction_profiles/htc/vive_controller",
      {
        left(touch.grip_squeeze,  "/trigger/value"),
        left(touch.grip_pose,     "/grip/pose"),
        left(touch.button_menu,   "/menu/click"),
        left(touch.vibrate,       "/haptic", false),

        right(touch.grip_squeeze, "/trigger/value"),
        right(touch.grip_pose,    "/grip/pose"),
        right(touch.button_system,"/menu/click"), //
        right(touch.vibrate,      "/haptic", false),
      }
    );

    // Valve Index Controller.
    suggestInteractionProfileBindings(
      "/interaction_profiles/valve/index_controller",
      {
        left(touch.grip_squeeze,   "/squeeze/value"),
        left(touch.grip_pose,      "/grip/pose"),
        left(touch.button_menu,    "/b/click"),
        left(touch.vibrate,        "/haptic", false),

        right(touch.grip_squeeze,  "/squeeze/value"),
        right(touch.grip_pose,     "/grip/pose"),
        right(touch.button_system, "/b/click"), //
        right(touch.vibrate,       "/haptic", false),
      }
    );
  }

  // -- Create Action Space.
  {
    auto createActionSpace{
      [&](XrAction action, XrPath path) {
        XrActionSpaceCreateInfo info{
          .type = XR_TYPE_ACTION_SPACE_CREATE_INFO,
          .action = action,
          .subactionPath = path,
          .poseInActionSpace = PoseIdentity(),
        };
        XrSpace action_space{XR_NULL_HANDLE};
        CHECK_XR(xrCreateActionSpace(session_, &info, &action_space));
        return action_space;
      }
    };
    for (auto side : {XRSide::Left, XRSide::Right}) {
      hand.aim_space[side] = createActionSpace(touch.aim_pose, hand.path[side]);
      hand.grip_space[side] = createActionSpace(touch.grip_pose, hand.path[side]);
    }
  }

  // -- Attach the action set to the session.
  {
    // Note :
    // This could be done only once per session, so we might want to be able
    // to skip it per application for user to provide custom action sets.
    XrSessionActionSetsAttachInfo attachInfo{
      .type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
      .countActionSets = 1u,
      .actionSets = &controls_.action_set,
    };
    CHECK_XR_RET(xrAttachSessionActionSets(session_, &attachInfo))
  }

  return true;
}

// ----------------------------------------------------------------------------

void OpenXRContext::createReferenceSpaces() {
  LOG_CHECK(XR_NULL_HANDLE != session_);

  bool const stageSupported{[this]()-> bool {
    uint32_t spaceCount(0u);
    CHECK_XR(xrEnumerateReferenceSpaces(session_, 0u, &spaceCount, nullptr));

    std::vector<XrReferenceSpaceType> spaces(spaceCount);
    CHECK_XR(xrEnumerateReferenceSpaces(session_, spaceCount, &spaceCount, spaces.data()));

    return std::any_of(spaces.cbegin(), spaces.cend(), [](auto const& space) {
      return space == XR_REFERENCE_SPACE_TYPE_STAGE;
    });
  }()};
  LOG_CHECK(stageSupported);

  XrReferenceSpaceCreateInfo create_info{
    .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
    .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW,
    .poseInReferenceSpace = PoseIdentity(),
  };
  // (head)
  CHECK_XR(xrCreateReferenceSpace(session_, &create_info, &spaces_[XRSpaceId::Head]));

  // (local)
  create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  CHECK_XR(xrCreateReferenceSpace(session_, &create_info, &spaces_[XRSpaceId::Local]));

  // (stage)
  create_info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
  create_info.poseInReferenceSpace.position.y = 0.0f;
  CHECK_XR(xrCreateReferenceSpace(session_, &create_info, &spaces_[XRSpaceId::Stage]));
}

/* -------------------------------------------------------------------------- */

void OpenXRContext::handleSessionStateChangedEvent(XrEventDataSessionStateChanged const& sessionStateChanged) {
  switch(sessionStateChanged.state) {
    case XR_SESSION_STATE_IDLE:
      LOGD("XR_SESSION_STATE_IDLE");
    break;
    
    case XR_SESSION_STATE_READY:
    {
      LOGD("XR_SESSION_STATE_READY");
      XrSessionBeginInfo sessionBeginInfo{
        .type = XR_TYPE_SESSION_BEGIN_INFO,
        .next = nullptr,
        .primaryViewConfigurationType = kViewConfigurationType, //
      };
      CHECK_XR(xrBeginSession(session_, &sessionBeginInfo));
      session_running_ = true;
    }
    break;

    case XR_SESSION_STATE_SYNCHRONIZED:
      LOGD("XR_SESSION_STATE_SYNCHRONIZED");
    break;

    case XR_SESSION_STATE_VISIBLE:
      LOGD("XR_SESSION_STATE_VISIBLE");
      session_focused_ = false;
    break;

    case XR_SESSION_STATE_FOCUSED:
      LOGD("XR_SESSION_STATE_FOCUSED");
      session_focused_ = true;
    break;

    case XR_SESSION_STATE_STOPPING:
    {
      LOGD("XR_SESSION_STATE_STOPPING");
      CHECK_XR(xrEndSession(session_));
      session_running_ = false;
    }
    break;

    case XR_SESSION_STATE_LOSS_PENDING:
      LOGD("XR_SESSION_STATE_LOSS_PENDING");
      request_restart_ = true;
      end_render_loop_ = true;
    break;

    case XR_SESSION_STATE_EXITING:
      LOGD("XR_SESSION_STATE_EXITING");
      request_restart_ = false;
      end_render_loop_ = true;
    break;
    
    default:
      LOGD("OpenXR Session state changed unprocessed : {}", (int)sessionStateChanged.state);
    break;
  }
}

// -----------------------------------------------------------------------------

void OpenXRContext::handleControls() {
  auto& touch{ controls_.action };

  // Exit session on pushing the "system" button.
  if (xrutils::HasButtonSwitched(session_, touch.button_system)) {
    CHECK_XR(xrRequestExitSession(session_));
  }

  // ---------------------------------

  auto& hand{ controls_.hand };
  auto& frame{ controls_.frame };
  XrTime const time{frame.state.predictedDisplayTime};

  // Head pose.
  // frame.head_pose = spaceLocation(spaces_[Space::Head], time).pose; //

  // Sided values.
  for (auto side : {XRSide::Left, XRSide::Right}) {
    auto const& path{ hand.path[side] };

    // Reset data.
    frame.active[side] = false;
    frame.aim_pose[side] = PoseIdentity();
    frame.grip_pose[side] = PoseIdentity();

    // Aim pose.
    if (xrutils::IsPoseActive(session_, touch.aim_pose, path)) {
      auto const& loc = spaceLocation(hand.aim_space[side], time); //
      frame.aim_pose[side] = loc.pose;
      frame.active[side] = (0 != (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT));
    }

    // Grip.
    if (xrutils::IsPoseActive(session_, touch.grip_pose, path)) {
      frame.grip_pose[side] = spaceLocation(hand.grip_space[side], time).pose; //
    }
    frame.grip_squeeze[side] = xrutils::GetFloat(session_, touch.grip_squeeze);

    // Trigger.
    frame.index_trigger[side] = xrutils::GetFloat(session_, touch.index_trigger);
    frame.touch_trigger[side] = xrutils::GetBoolean(session_, touch.touch_trigger);

    // Thumbstick.
    frame.button_thumbstick[side] = xrutils::GetBoolean(session_, touch.click_joystick);
    frame.touch_thumbstick[side]  = xrutils::GetBoolean(session_, touch.touch_joystick);
  }

  frame.button_x      = xrutils::HasButtonSwitched(session_, touch.button_x);
  frame.button_y      = xrutils::HasButtonSwitched(session_, touch.button_y);
  frame.touch_x       = xrutils::GetBoolean(session_, touch.touch_x);
  frame.touch_y       = xrutils::GetBoolean(session_, touch.touch_y);
  frame.button_menu   = xrutils::GetBoolean(session_, touch.button_menu);

  frame.button_a      = xrutils::HasButtonSwitched(session_, touch.button_a);
  frame.button_b      = xrutils::HasButtonSwitched(session_, touch.button_b);
  frame.touch_a       = xrutils::GetBoolean(session_, touch.touch_a);
  frame.touch_b       = xrutils::GetBoolean(session_, touch.touch_b);
  frame.button_system = xrutils::GetBoolean(session_, touch.button_system);
}

/* -------------------------------------------------------------------------- */
