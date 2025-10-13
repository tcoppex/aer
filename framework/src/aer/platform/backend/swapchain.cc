#include "aer/platform/backend/swapchain.h"
#include "aer/platform/backend/context.h"

/* -------------------------------------------------------------------------- */

namespace {

bool IsSwapchainInvalid(VkResult const result, std::string_view msg) {
  switch (result) {
    case VK_SUCCESS:
      return false;

    case VK_SUBOPTIMAL_KHR:
      LOGW("The current swapchain is suboptimal ({}).", msg);
      return false;

    case VK_ERROR_OUT_OF_DATE_KHR:
      LOGW("[TODO] The swapchain need to be rebuilt ({}).", msg);
      return true;

    default:
      LOGE("{} : swapchain image issue.", msg);
     return true;
  }
}

}

/* -------------------------------------------------------------------------- */

void Swapchain::init(Context const& context, VkSurfaceKHR surface) {
  LOG_CHECK(VK_NULL_HANDLE != surface);
  LOG_CHECK(vkGetPhysicalDeviceSurfaceCapabilities2KHR);
  gpu_ = context.physical_device();
  device_ = context.device();

  // ---------

  /* Retrieve the GPU's capabilities for this surface. */
  VkPhysicalDeviceSurfaceInfo2KHR const surface_info2{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR,
    .surface = surface,
  };
  VkSurfaceCapabilities2KHR capabilities2{
    .sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR
  };
  vkGetPhysicalDeviceSurfaceCapabilities2KHR(gpu_, &surface_info2, &capabilities2);
  auto const surface_capabilities = capabilities2.surfaceCapabilities;

  // ---------

  /* Configure the swapchain creation info. */
  if ((handle_ == VK_NULL_HANDLE) && (swapchain_create_info_.oldSwapchain == VK_NULL_HANDLE))
  {
    // [we do not need to repeat this step when we kept the previous swapchain]

    /* Determine the number of image to use in the swapchain. */
    uint32_t const min_image_count{ surface_capabilities.minImageCount };
    uint32_t const preferred_image_count{ std::max(kPreferredMaxImageCount, min_image_count) };
    uint32_t const max_image_count{
      (surface_capabilities.maxImageCount == 0u) ? preferred_image_count
                                                 : surface_capabilities.maxImageCount
    };
    image_count_ = std::clamp(preferred_image_count, min_image_count, max_image_count);
    LOGD("image count : {}", image_count_);

    /* Select best swap surface format and present mode. */
    auto const surface_format = select_surface_format(&surface_info2).surfaceFormat;

    swapchain_create_info_ = VkSwapchainCreateInfoKHR{
      .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext            = nullptr,
      .flags            = VkSwapchainCreateFlagsKHR{},
      .surface          = VK_NULL_HANDLE,
      .minImageCount    = image_count_,
      .imageFormat      = surface_format.format,
      .imageColorSpace  = surface_format.colorSpace,
      .imageExtent      = {},
      .imageArrayLayers = 1u,
      .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                        | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                        ,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0u,
      .pQueueFamilyIndices = nullptr,
      .preTransform     = surface_capabilities.currentTransform,
      .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode      = select_present_mode(surface, kUseVSync),
      .clipped          = VK_TRUE,
      .oldSwapchain     = VK_NULL_HANDLE,
    };

    /* Build timeline resources */
    if (timeline_.semaphore == VK_NULL_HANDLE)
    {
      timeline_.signal_indices.resize(image_count_);
      for (uint64_t i = 0u; i < image_count_; ++i) {
        timeline_.signal_indices[i] = i;
      }
      // Create the timeline semaphore.
      VkSemaphoreTypeCreateInfo const semaphore_type_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = image_count_ - 1u,
      };
      VkSemaphoreCreateInfo const semaphore_create_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &semaphore_type_create_info,
      };
      CHECK_VK(vkCreateSemaphore(
        device_, &semaphore_create_info, nullptr, &timeline_.semaphore
      ));
    }
  }

  swapchain_create_info_.surface      = surface;
  swapchain_create_info_.imageExtent  = surface_capabilities.currentExtent;
  swapchain_create_info_.oldSwapchain = handle_;

  // ---------

  /* Create the swapchain image. */
  handle_ = VK_NULL_HANDLE;
  CHECK_VK(vkCreateSwapchainKHR(
    device_, &swapchain_create_info_, nullptr, &handle_
  ));
  context.set_debug_object_name(handle_, "Swapchain");

  /* Create the swapchain resources (Views + Semaphores). */
  std::vector<VkImage> images(image_count_);
  CHECK_VK(vkGetSwapchainImagesKHR(
    device_, handle_, &image_count_, images.data()
  ));

  // Resizing everything to the exact in flight image count.
  images.resize(image_count_);
  images_.resize(image_count_);
  synchronizers_.resize(image_count_);

  VkImageViewCreateInfo image_view_create_info{
    .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType   = VK_IMAGE_VIEW_TYPE_2D,
    .format     = swapchain_create_info_.imageFormat,
    .components = {
      .r = VK_COMPONENT_SWIZZLE_IDENTITY,
      .g = VK_COMPONENT_SWIZZLE_IDENTITY,
      .b = VK_COMPONENT_SWIZZLE_IDENTITY,
      .a = VK_COMPONENT_SWIZZLE_IDENTITY,
    },
    .subresourceRange = {
      .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel   = 0u,
      .levelCount     = 1u,
      .baseArrayLayer = 0u,
      .layerCount     = 1u,
    },
  };
  VkSemaphoreCreateInfo const semaphore_create_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
  };

  for (uint32_t i = 0u; i < image_count_; ++i) {
    auto &buffer = images_[i];
    buffer.image = images[i];
    buffer.format = swapchain_create_info_.imageFormat;
    image_view_create_info.image = buffer.image;
    CHECK_VK(vkCreateImageView(device_, &image_view_create_info, nullptr, &buffer.view));

    auto &sync = synchronizers_[i];
    CHECK_VK(vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &sync.wait_image_semaphore));
    CHECK_VK(vkCreateSemaphore(device_, &semaphore_create_info, nullptr, &sync.signal_present_semaphore));

#if !defined(NDEBUG)
    auto const s_index = std::to_string(i);
    context.set_debug_object_name(buffer.image,
      "Swapchain::Image::" + s_index
    );
    context.set_debug_object_name(buffer.view,
      "Swapchain::ImageView::" + s_index
    );
    context.set_debug_object_name(sync.wait_image_semaphore,
      "Swapchain::Semaphore::WaitImage::" + s_index
    );
    context.set_debug_object_name(sync.signal_present_semaphore,
      "Swapchain::Semaphore::SignalPresent::" + s_index
    );
#endif
  }

  /* When using timeline semaphore, we need to transition images layout to present. */
  context.transition_images_layout(
    images_,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    imageArraySize()
  );
  need_rebuild_ = false;
}

// ----------------------------------------------------------------------------

void Swapchain::deinit(bool keep_previous_swapchain) {
  LOGD("Destroy swapchain resources,{} keep previous handle.",
    keep_previous_swapchain ? "" : " don't"
  );

  for (auto& buffer : images_) {
    vkDestroyImageView(device_, buffer.view, nullptr);
  }
  images_.clear();

  for (auto& frame_sync : synchronizers_) {
    vkDestroySemaphore(device_, frame_sync.wait_image_semaphore, nullptr);
    vkDestroySemaphore(device_, frame_sync.signal_present_semaphore, nullptr);
  }
  synchronizers_.clear();

  if (swapchain_create_info_.oldSwapchain != VK_NULL_HANDLE) [[likely]] {
    vkDestroySwapchainKHR(device_, swapchain_create_info_.oldSwapchain, nullptr);
    swapchain_create_info_.oldSwapchain = VK_NULL_HANDLE;
  }

  if (!keep_previous_swapchain) [[unlikely]] {
    vkDestroySwapchainKHR(device_, handle_, nullptr);
    handle_ = VK_NULL_HANDLE;

    vkDestroySemaphore(device_, timeline_.semaphore, nullptr);
    timeline_.semaphore = VK_NULL_HANDLE;
  }

  need_rebuild_ = true;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

bool Swapchain::acquireNextImage() {
  LOG_CHECK(handle_ != VK_NULL_HANDLE);

  // Wait for the GPU to have finished using this frame resources.
  VkSemaphoreWaitInfo const semaphore_wait_info{
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .semaphoreCount = 1u,
    .pSemaphores = &timeline_.semaphore,
    .pValues = timeline_signal_index_ptr(),
  };
  CHECK_VK(vkWaitSemaphores(device_, &semaphore_wait_info, UINT64_MAX));

  constexpr uint64_t kFiniteAcquireTimeout = 1'000'000'000ull; // 1s
  auto const acquire_result = vkAcquireNextImageKHR(
    device_,
    handle_,
    kFiniteAcquireTimeout,
    wait_image_semaphore(),
    VK_NULL_HANDLE,
    &acquired_image_index_
  );
  need_rebuild_ = IsSwapchainInvalid(acquire_result, __FUNCTION__);

  return isValid();
}

// ----------------------------------------------------------------------------

bool Swapchain::submitFrame(VkQueue queue, VkCommandBuffer command_buffer) {
  LOG_CHECK(handle_ != VK_NULL_HANDLE);

  VkPipelineStageFlags2 constexpr kStageMask{
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
  };

  // Next frame index to start when this one completed.
  uint64_t *signal_index = timeline_signal_index_ptr();
  *signal_index += static_cast<uint64_t>(imageCount());

  // Semaphore(s) to wait for:
  //    - Image available.
  std::vector<VkSemaphoreSubmitInfo> const wait_semaphores{
    {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = wait_image_semaphore(),
      .stageMask = kStageMask,
    },
  };

  // Array of command buffers to submit (here, just one).
  std::vector<VkCommandBufferSubmitInfo> const cb_submit_infos{
    {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = command_buffer,
    },
  };

  // Semaphores to signal when terminating:
  //    - Ready to present,
  //    - Next frame to render,
  std::vector<VkSemaphoreSubmitInfo> const signal_semaphores{
    {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = signal_present_semaphore(),
      .stageMask = kStageMask,
    },
    {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = timeline_.semaphore,
      .value = *signal_index,
      .stageMask = kStageMask,
    },
  };

  VkSubmitInfo2 const submit_info_2{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount = static_cast<uint32_t>(wait_semaphores.size()),
    .pWaitSemaphoreInfos = wait_semaphores.data(),
    .commandBufferInfoCount = static_cast<uint32_t>(cb_submit_infos.size()),
    .pCommandBufferInfos = cb_submit_infos.data(),
    .signalSemaphoreInfoCount = static_cast<uint32_t>(signal_semaphores.size()),
    .pSignalSemaphoreInfos = signal_semaphores.data(),
  };
  CHECK_VK( vkQueueSubmit2(queue, 1u, &submit_info_2, nullptr) );

  return isValid();
}

// ----------------------------------------------------------------------------

bool Swapchain::finishFrame(VkQueue queue) {
  auto present_semaphore = signal_present_semaphore();

  VkPresentInfoKHR const present_info{
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .pNext = nullptr,
    .waitSemaphoreCount = 1u,
    .pWaitSemaphores = &present_semaphore,
    .swapchainCount = 1u,
    .pSwapchains = &handle_,
    .pImageIndices = &acquired_image_index_,
    .pResults = nullptr,
  };
  auto const present_result = vkQueuePresentKHR(queue, &present_info);
  need_rebuild_ = IsSwapchainInvalid(present_result, __FUNCTION__);

  swap_index_ = (swap_index_ + 1u) % image_count_;

  return isValid();
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

VkSurfaceFormat2KHR Swapchain::select_surface_format(
  VkPhysicalDeviceSurfaceInfo2KHR const* surface_info2
) const {
  LOG_CHECK(vkGetPhysicalDeviceSurfaceFormats2KHR);

#ifdef ANDROID
  LOGV("> Start vkGetPhysicalDeviceSurfaceFormats2KHR");
#endif

  uint32_t image_format_count{0u};
  CHECK_VK(vkGetPhysicalDeviceSurfaceFormats2KHR(
    gpu_, surface_info2, &image_format_count, nullptr)
  );
  std::vector<VkSurfaceFormat2KHR> formats(
    image_format_count, {.sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR}
  );
  CHECK_VK(vkGetPhysicalDeviceSurfaceFormats2KHR(
    gpu_, surface_info2, &image_format_count, formats.data()
  ));
  // for (auto const& available : formats) {
  //   LOGD("- {} {}", (int)available.surfaceFormat.format, (int)available.surfaceFormat.colorSpace);
  // }

#ifdef ANDROID
  LOGV("> End vkGetPhysicalDeviceSurfaceFormats2KHR");
#endif

  std::vector<VkSurfaceFormat2KHR> const preferred_formats{
    VkSurfaceFormat2KHR{
      .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR,
      .surfaceFormat = {
        .format = VK_FORMAT_B8G8R8A8_UNORM,
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
      },
    },
    VkSurfaceFormat2KHR{
      .sType = VK_STRUCTURE_TYPE_SURFACE_FORMAT_2_KHR,
      .surfaceFormat = {
        .format = VK_FORMAT_B8G8R8A8_SRGB,    //< (should probably be preferred)
        .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
      },
    },
  };

  for (auto const& preferred : preferred_formats) {
    for (auto const& available : formats) {
      if ((available.surfaceFormat.format == preferred.surfaceFormat.format)
       && (available.surfaceFormat.colorSpace == preferred.surfaceFormat.colorSpace)) {
        // LOGD("Chosen format : {}", (int)available.surfaceFormat.format);
        return available;
      }
    }
  }

  return formats[0];
}

// ----------------------------------------------------------------------------

VkPresentModeKHR Swapchain::select_present_mode(VkSurfaceKHR surface, bool use_vsync) const {
  if (use_vsync) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  uint32_t present_mode_count{0u};
  vkGetPhysicalDeviceSurfacePresentModesKHR(
    gpu_, surface, &present_mode_count, nullptr
  );
  std::vector<VkPresentModeKHR> present_modes(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
    gpu_, surface, &present_mode_count, present_modes.data()
  );

  bool mailbox_available = false;
  bool immediate_available = false;

  for (auto mode : present_modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      mailbox_available = true;
    }
    if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
      immediate_available = true;
    }
  }

  // Best (low latency with no tears)
  if (mailbox_available) {
    return VK_PRESENT_MODE_MAILBOX_KHR;
  }

  // Second best (low latency, with tears)
  if (immediate_available) {
    return VK_PRESENT_MODE_IMMEDIATE_KHR;
  }

  // Default mode available everywhere.
  return VK_PRESENT_MODE_FIFO_KHR;
}

/* -------------------------------------------------------------------------- */
