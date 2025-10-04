#pragma once

#include <vector>
#include "volk.h"

#include "framework/backend/types.h" // (for backend::Image)

/* -------------------------------------------------------------------------- */

class SwapchainInterface {
 public:
  virtual ~SwapchainInterface() = default;

  virtual bool acquireNextImage() = 0;

  // [todo: transform to accept a span of VkCommandBuffer]
  virtual bool submitFrame(VkQueue queue, VkCommandBuffer command_buffer) = 0;

  virtual bool finishFrame(VkQueue queue) = 0;

  virtual VkExtent2D surfaceSize() const noexcept = 0;

  virtual uint32_t imageCount() const noexcept = 0;

  virtual VkFormat format() const noexcept = 0;

  virtual uint32_t viewMask() const noexcept = 0;

  virtual backend::Image currentImage() const noexcept = 0;
};

/* -------------------------------------------------------------------------- */
