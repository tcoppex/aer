#ifndef AER_SCENE_SAMPLER_H_
#define AER_SCENE_SAMPLER_H_

#include "aer/platform/vulkan/vulkan_wrapper.h" // for VkSamplerCreateInfo..

namespace scene {

/* -------------------------------------------------------------------------- */

struct Sampler {
  Sampler() = default;

  Sampler(VkSamplerCreateInfo _info)
    : info(_info)
    , set_(true)
  {}

  bool use_default() const {
    return !set_;
  }

  // (should be changed to not use Vulkan internally)
  VkSamplerCreateInfo info{}; //

 private:
  bool set_{};
};

/* -------------------------------------------------------------------------- */

}  // namespace scene

#endif // AER_SCENE_SAMPLER_H_
