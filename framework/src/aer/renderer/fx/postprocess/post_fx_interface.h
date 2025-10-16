#ifndef AER_RENDERER_FX_POST_FX_INTERFACE_H_
#define AER_RENDERER_FX_POST_FX_INTERFACE_H_

#include "aer/renderer/fx/postprocess/fx_interface.h"

/* -------------------------------------------------------------------------- */

class PostFxInterface : public virtual FxInterface {
 public:
  virtual ~PostFxInterface() {}

 public:
  virtual bool resize(VkExtent2D const dimension) = 0;

  virtual backend::Image image_output(uint32_t index = 0u) const = 0;

  virtual std::vector<backend::Image> image_outputs() const = 0;

  virtual backend::Buffer buffer_output(uint32_t index = 0u) const = 0;

  virtual std::vector<backend::Buffer> buffer_outputs() const = 0;

 protected:
  // virtual void releaseImagesAndBuffers() = 0;
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POST_FX_INTERFACE_H_
