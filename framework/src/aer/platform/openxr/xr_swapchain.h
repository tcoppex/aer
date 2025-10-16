#pragma once

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/openxr/xr_common.h"
#include "aer/platform/openxr/xr_vulkan_interface.h" //
#include "aer/platform/swapchain_interface.h" //

/* -------------------------------------------------------------------------- */

struct OpenXRSwapchain : public SwapchainInterface {
 public:
  virtual ~OpenXRSwapchain() = default;

  bool acquireNextImage() final;

  bool submitFrame(VkQueue queue, VkCommandBuffer command_buffer) final;

  bool finishFrame(VkQueue queue) final;

  VkExtent2D surface_size() const noexcept final {
    return {
      .width = create_info_.width,
      .height = create_info_.height,
    };
  }

  uint32_t image_count() const noexcept final {
    return image_count_;
  }

  VkFormat format() const noexcept final {
    return (VkFormat)create_info_.format;
  }

  uint32_t view_mask() const noexcept final {
    LOG_CHECK(create_info_.arraySize > 1u);
    return 0b11;
  }

  backend::Image current_image() const noexcept final {
    return images_[current_image_index_];
  }

 public:
  bool create(
    XrSession session,
    XrSwapchainCreateInfo const& info,
    XRVulkanInterface *xr_graphics //
  );

  void destroy();

  [[nodiscard]]
  XrExtent2Di extent() const noexcept {
    return {
      .width = static_cast<int32_t>(create_info_.width),
      .height = static_cast<int32_t>(create_info_.height)
    };
  }

  [[nodiscard]]
  XrRect2Di rect() const noexcept {
    return {
      .offset = XrOffset2Di{0, 0},
      .extent = extent()
    };
  }

  [[nodiscard]]
  XrSwapchain handle() const noexcept {
    return handle_;
  }

 private:
  XrSwapchainCreateInfo create_info_{};
  XrSwapchain handle_{XR_NULL_HANDLE};
  std::vector<backend::Image> images_{};
  uint32_t image_count_{};
  uint32_t current_image_index_{};

  XRVulkanInterface *xr_graphics_{};
};

/* -------------------------------------------------------------------------- */
