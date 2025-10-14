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

  VkExtent2D surfaceSize() const noexcept final {
    return {
      .width = create_info.width,
      .height = create_info.height,
    };
  }

  uint32_t imageCount() const noexcept final {
    return image_count;
  }

  VkFormat format() const noexcept final {
    return (VkFormat)create_info.format;
  }

  uint32_t viewMask() const noexcept final {
    LOG_CHECK(create_info.arraySize > 1u);
    return 0b11;
  }

  backend::Image currentImage() const noexcept final {
    return images[current_image_index];
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
      .width = static_cast<int32_t>(create_info.width),
      .height = static_cast<int32_t>(create_info.height)
    };
  }

  [[nodiscard]]
  XrRect2Di rect() const noexcept {
    return {
      .offset = XrOffset2Di{0, 0},
      .extent = extent()
    };
  }

 public:
  XrSwapchainCreateInfo create_info{};
  XrSwapchain handle{XR_NULL_HANDLE};
  std::vector<backend::Image> images{};
  uint32_t image_count{};
  uint32_t current_image_index{};

 private:
  XRVulkanInterface *xr_graphics_{};
};

/* -------------------------------------------------------------------------- */
