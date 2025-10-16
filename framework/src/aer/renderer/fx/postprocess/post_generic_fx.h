#ifndef AER_RENDERER_FX_POSTPROCESS_POST_GENERIC_FX_H_
#define AER_RENDERER_FX_POSTPROCESS_POST_GENERIC_FX_H_

#include "aer/renderer/fx/postprocess/generic_fx.h"
#include "aer/renderer/fx/postprocess/post_fx_interface.h"

/* -------------------------------------------------------------------------- */

class PostGenericFx : public virtual GenericFx
                    , public PostFxInterface {
 public:
  void setup(VkExtent2D const dimension) override {
    resize(dimension);
    GenericFx::setup(dimension);
    enabled_ = true;
  }

 public:
  bool is_enable() const {
    return enabled_;
  }

  void enable(bool status) {
    enabled_ = status;
  }

  bool enabled_{false};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_POST_GENERIC_FX_H_
