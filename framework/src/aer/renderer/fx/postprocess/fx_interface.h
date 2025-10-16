#ifndef AER_RENDERER_FX_FX_INTERFACE_H_
#define AER_RENDERER_FX_FX_INTERFACE_H_

#include "aer/core/common.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/platform/vulkan/types.h"

class RenderContext;

/* -------------------------------------------------------------------------- */

class FxInterface {
 public:
  FxInterface() = default;

  virtual ~FxInterface() = default;

  virtual void init(RenderContext const& context) = 0;

  virtual void setup(VkExtent2D const dimension) = 0; //

  virtual void release() = 0;

  virtual void setupUI() = 0;

  virtual void execute(CommandEncoder const& cmd) const = 0;

  virtual void set_image_inputs(std::vector<backend::Image> const& inputs) = 0;

  virtual void set_image_input(backend::Image const& input) {
    set_image_inputs({ input });
  }

  virtual void set_buffer_inputs(std::vector<backend::Buffer> const& inputs) = 0;

  virtual void set_buffer_input(backend::Buffer const& input) {
    set_buffer_inputs({ input });
  }
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_FX_INTERFACE_H_
