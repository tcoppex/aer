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

  // -----------------

  virtual void setImageInputs(std::vector<backend::Image> const& inputs) = 0;

  virtual void setImageInput(backend::Image const& input) {
    setImageInputs({ input });
  }

  virtual void setBufferInputs(std::vector<backend::Buffer> const& inputs) = 0;

  virtual void setBufferInput(backend::Buffer const& input) {
    setBufferInputs({ input });
  }

  virtual void execute(CommandEncoder const& cmd) const = 0;
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_FX_INTERFACE_H_
