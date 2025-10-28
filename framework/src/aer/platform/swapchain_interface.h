#ifndef AER_PLATEFORM_SWAPCHAIN_INTERFACE_H_
#define AER_PLATEFORM_SWAPCHAIN_INTERFACE_H_

#include <vector>
#include "aer/platform/vulkan/vulkan_wrapper.h"
#include "aer/platform/vulkan/types.h" // (for backend::Image)

/* -------------------------------------------------------------------------- */

class SwapchainInterface {
 public:
  virtual ~SwapchainInterface() = default;

  virtual bool acquireNextImage() = 0;

  // [todo: transform to accept a span of VkCommandBuffer]
  virtual bool submitFrame(VkQueue queue, VkCommandBuffer command_buffer) = 0;

  virtual bool finishFrame(VkQueue queue) = 0;

  virtual bool is_valid() const noexcept { return true; }

  virtual VkExtent2D surface_size() const noexcept = 0;

  virtual uint32_t image_count() const noexcept = 0;

  virtual VkFormat format() const noexcept = 0;

  virtual uint32_t view_mask() const noexcept = 0;

  virtual uint32_t image_array_size() const noexcept {
    return (view_mask() > 0) ? 2u : 1u;
  }

  virtual backend::Image current_image() const noexcept = 0;
};

/* -------------------------------------------------------------------------- */

#endif // AER_PLATEFORM_SWAPCHAIN_INTERFACE_H_
