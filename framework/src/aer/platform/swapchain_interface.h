#ifndef AER_PLATEFORM_SWAPCHAIN_INTERFACE_H_
#define AER_PLATEFORM_SWAPCHAIN_INTERFACE_H_

#include <vector>
#include "aer/platform/vulkan/vulkan_wrapper.h"
#include "aer/platform/vulkan/types.h" // (for backend::Image)

/* -------------------------------------------------------------------------- */

// (could probably inherit RTInterface too..)
class SwapchainInterface {
 public:
  virtual ~SwapchainInterface() = default;

  virtual bool acquireNextImage() = 0;

  // [todo: transform to accept a span of VkCommandBuffer]
  virtual bool submitFrame(VkQueue queue, VkCommandBuffer command_buffer) = 0;

  virtual bool finishFrame(VkQueue queue) = 0;

  // -----------------------------

  virtual VkExtent2D surfaceSize() const noexcept = 0;

  virtual uint32_t imageCount() const noexcept = 0;

  virtual VkFormat format() const noexcept = 0;

  virtual uint32_t viewMask() const noexcept = 0;

  virtual uint32_t imageArraySize() const noexcept {
    return (viewMask() > 0) ? 2u : 1u;
  }

  virtual backend::Image currentImage() const noexcept = 0;
};

/* -------------------------------------------------------------------------- */

#endif // AER_PLATEFORM_SWAPCHAIN_INTERFACE_H_
