#ifndef AER_SCENE_TEXTURE_H_
#define AER_SCENE_TEXTURE_H_

#include "aer/scene/sampler.h"

namespace scene {

/* -------------------------------------------------------------------------- */

struct Texture {
 public:
  Texture() = default;

  Texture(uint32_t _host_image_index)
    : host_image_index(_host_image_index)
  {}

  Texture(uint32_t _host_image_index, Sampler _sampler)
    : host_image_index(_host_image_index)
    , sampler(_sampler)
  {}

  uint32_t channel_index() const {
    return host_image_index;
  }

  uint32_t host_image_index{kInvalidIndexU32};
  Sampler sampler{}; // (or sampler_index ?)
};

/* -------------------------------------------------------------------------- */

}  // namespace scene

#endif // AER_SCENE_TEXTURE_H_
