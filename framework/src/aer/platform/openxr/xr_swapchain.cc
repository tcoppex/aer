
#include "aer/platform/openxr/xr_swapchain.h" //

/* -------------------------------------------------------------------------- */

bool OpenXRSwapchain::acquireNextImage() {
  XrSwapchainImageAcquireInfo acquire_info{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  CHECK_XR_RET(xrAcquireSwapchainImage(
    handle, &acquire_info, &current_image_index
  ));
  XrSwapchainImageWaitInfo wait_info{
    .type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
    .timeout = XR_INFINITE_DURATION,
  };
  CHECK_XR_RET(xrWaitSwapchainImage(handle, &wait_info));
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
  CHECK_XR_RET(xrReleaseSwapchainImage(handle, &releaseInfo));
  return true;
}

// ----------------------------------------------------------------------------

bool OpenXRSwapchain::create(
  XrSession session,
  XrSwapchainCreateInfo const& info,
  XRVulkanInterface *xr_graphics //
) {
  LOG_CHECK( xr_graphics != nullptr );

  create_info = info;

  // Create the swapchain object.
  CHECK_XR_RET(xrCreateSwapchain(session, &info, &handle))

  // Retrieve swapchain images.
  CHECK_XR(xrEnumerateSwapchainImages(handle, 0, &image_count, nullptr));
  std::vector<XrSwapchainImageVulkanKHR> base_images(image_count, {
    .type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR
  });
  CHECK_XR(xrEnumerateSwapchainImages(
    handle, image_count, &image_count,
    reinterpret_cast<XrSwapchainImageBaseHeader*>(base_images.data())
  ));

  auto const aspect_mask =
      (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
          ? VK_IMAGE_ASPECT_DEPTH_BIT
          : VK_IMAGE_ASPECT_COLOR_BIT
          ;
  auto view_info = VkImageViewCreateInfo{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = (info.arraySize > 1u) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                      : VK_IMAGE_VIEW_TYPE_2D,
    .format = static_cast<VkFormat>(info.format),
    .components = {
      VK_COMPONENT_SWIZZLE_R,
      VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
    },
    .subresourceRange = {
      .aspectMask = static_cast<VkImageAspectFlags>(aspect_mask),
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = info.arraySize,
    }
  };
  xr_graphics->allocateSwapchainImage(base_images, view_info, images);

  return true;
}

// ----------------------------------------------------------------------------

void OpenXRSwapchain::destroy(XRVulkanInterface *xr_graphics) {
  LOG_CHECK( xr_graphics != nullptr );
  
  xr_graphics->releaseSwapchainImage(images);
  images.clear();
  if (handle != XR_NULL_HANDLE) {
    xrDestroySwapchain(handle);
    handle = XR_NULL_HANDLE;
  }
}

/* -------------------------------------------------------------------------- */
