#ifndef AER_PLATEFORM_UI_UI_CONTROLLER_H_
#define AER_PLATEFORM_UI_UI_CONTROLLER_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/vulkan/context.h"

#include "aer/platform/wm_interface.h"
#include "aer/platform/imgui_wrapper.h" //

class Renderer;
class CommandEncoder;

/* -------------------------------------------------------------------------- */

class UIController {
 public:
  UIController() = default;
  virtual ~UIController() {}

  [[nodiscard]]
  bool init(Renderer const& renderer, WMInterface const& wm);

  void release(Context const& context);

  void beginFrame();

  void endFrame();

  void draw(CommandEncoder const& cmd, VkImageView image_view, VkExtent2D surface_size);

 protected:
  virtual void setupStyles();

 private:
  WMInterface const* wm_ptr_{};
  VkDescriptorPool imgui_descriptor_pool_{}; //

  // std::unique_ptr<backend::RTInterface> render_target_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_PLATEFORM_UI_UI_CONTROLLER_H_