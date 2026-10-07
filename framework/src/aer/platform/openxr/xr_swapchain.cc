
#include "aer/platform/openxr/xr_swapchain.h" //

/* -------------------------------------------------------------------------- */

bool OpenXRSwapchain::acquireNextImage() {
  XrSwapchainImageAcquireInfo acquire_info{
    XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO
  };
  CHECK_XR_RET(xrAcquireSwapchainImage(
    handle_, &acquire_info, &current_image_index_
  ));
  XrSwapchainImageWaitInfo wait_info{
    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
    .timeout = XR_INFINITE_DURATION,
  };
  CHECK_XR_RET(xrWaitSwapchainImage(handle_, &wait_info));
  return true;
}

// ----------------------------------------------------------------------------

bool OpenXRSwapchain::submitFrame(VkQueue queue, VkCommandBuffer command_buffer) {
  std::vector<VkCommandBufferSubmitInfo> const cb_submit_infos{{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .commandBuffer = command_buffer,
  }};
  VkSubmitInfo2 const submit_info_2{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .commandBufferInfoCount = static_cast<uint32_t>(cb_submit_infos.size()),
    .pCommandBufferInfos = cb_submit_infos.data(),
  };
  return vkQueueSubmit2(queue, 1u, &submit_info_2, nullptr) == VK_SUCCESS;
}

// ----------------------------------------------------------------------------

bool OpenXRSwapchain::finishFrame(VkQueue queue) {
  XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  CHECK_XR_RET(xrReleaseSwapchainImage(handle_, &releaseInfo))
  return true;
}

// ----------------------------------------------------------------------------

bool OpenXRSwapchain::create(
  XrSession session,
  XrSwapchainCreateInfo const& info,
  XRVulkanInterface* xr_graphics,
  bool use_foveation
) {
  LOG_CHECK(handle_ == XR_NULL_HANDLE);
  LOG_CHECK(xr_graphics != nullptr);
  create_info_ = info;
  xr_graphics_ = xr_graphics;

  CHECK_XR_RET(xrCreateSwapchain(session, &info, &handle_))

  bool res = xr_graphics_->createSwapchainImages(
    handle_, create_info_, use_foveation, images_, fdms_
  );
  image_count_ = static_cast<uint32_t>(images_.size());

  return res;
}

// ----------------------------------------------------------------------------

void OpenXRSwapchain::destroy() {
  if (xr_graphics_) {
    xr_graphics_->destroySwapchainImages(fdms_);
    xr_graphics_->destroySwapchainImages(images_);
  }
  if (handle_ != XR_NULL_HANDLE) {
    xrDestroySwapchain(handle_);
    handle_ = XR_NULL_HANDLE;
  }
}

/* -------------------------------------------------------------------------- */
