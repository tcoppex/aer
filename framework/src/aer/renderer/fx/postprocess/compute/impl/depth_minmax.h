#ifndef AER_RENDERER_FX_POSTPROCESS_COMPUTE_IMPL_DEPTH_MINMAX_H_
#define AER_RENDERER_FX_POSTPROCESS_COMPUTE_IMPL_DEPTH_MINMAX_H_

#include "aer/renderer/fx/postprocess/compute/compute_fx.h"

/* -------------------------------------------------------------------------- */

namespace fx::compute {

class DepthMinMax final : public ComputeFx {
 public:
  bool resize(VkExtent2D const dimension) final {
    if (!ComputeFx::resize(dimension)) {
      return false;
    }

    // (malformed, should use internal method to update descriptor..)
    buffers_.push_back(context_ptr_->createBuffer(
      2u * sizeof(float),
        VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR
    ));

    return true;
  }

 protected:
  std::string shader_name() const final {
    return FRAMEWORK_COMPILED_SHADERS_DIR "postprocess/depth_minmax.comp.glsl";
  }
};

}

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_COMPUTE_IMPL_DEPTH_MINMAX_H_
