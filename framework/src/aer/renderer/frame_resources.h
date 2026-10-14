#ifndef AER_RENDERER_FRAME_RESOURCES_H_
#define AER_RENDERER_FRAME_RESOURCES_H_

#include "aer/platform/vulkan/types.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/targets/render_target.h"

#include <memory>

/* -------------------------------------------------------------------------- */

struct FrameResources {
  VkCommandPool command_pool{};
  VkCommandBuffer command_buffer{};
  CommandEncoder cmd{}; //

  std::unique_ptr<backend::RTInterface> main_rt{}; //

 public:
  [[nodiscard]]
  RenderTarget::Descriptor rt_descriptor() const noexcept {
    return rt_descriptor_;
  }

  void set_rt_descriptor(RenderTarget::Descriptor const& desc) {
    rt_descriptor_ = desc;
  }

 private:
  RenderTarget::Descriptor rt_descriptor_{};
};

// ----------------------------------------------------------------------------

using FrameResourcesBuffer = std::vector<FrameResources>;

/* -------------------------------------------------------------------------- */


#endif // AER_RENDERER_FRAME_RESOURCES_H_