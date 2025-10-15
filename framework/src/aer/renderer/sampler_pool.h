#ifndef AER_RENDERER_SAMPLER_POOL_H_
#define AER_RENDERER_SAMPLER_POOL_H_

#include "aer/core/common.h"
#include "aer/core/utils.h"
#include "aer/scene/sampler.h"

/* -------------------------------------------------------------------------- */

class SamplerPool {
 public:
  SamplerPool() = default;

  ~SamplerPool() {
    LOG_CHECK( device_ == VK_NULL_HANDLE );
  }

  void init(VkDevice device) noexcept {
    device_ = device;

    default_sampler_ = get({
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .anisotropyEnable = VK_TRUE,
      .maxAnisotropy = 16.0f,
      .maxLod = VK_LOD_CLAMP_NONE,
    });
  }

  void release() {
    for (auto const& [_, sampler] : map_) {
      vkDestroySampler(device_, sampler, nullptr);
    }
    map_.clear();
    *this = {};
  }

  [[nodiscard]]
  VkSampler default_sampler() const noexcept {
    return default_sampler_;
  }

  [[nodiscard]]
  VkSampler get(VkSamplerCreateInfo info) const {
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    if (auto it = map_.find(info); it != map_.end()) {
      return it->second;
    }
    VkSampler sampler = createSampler(info);
    map_[info] = sampler;
    return sampler;
  }

  [[nodiscard]]
  VkSampler convert(scene::Sampler const& scene_sampler) const {
    return scene_sampler.use_default() ? default_sampler_
                                       : get(scene_sampler.info)
                                       ;
  }

  void destroy(VkSampler sampler) {
    for (auto it = map_.begin(); it != map_.end(); ) {
      if (it->second == sampler) {
        vkDestroySampler(device_, it->second, nullptr);
        it = map_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  struct InfoHash {
    size_t operator()(VkSamplerCreateInfo const& info) const {
      size_t h(0);
      h = utils::HashCombine(h, info.magFilter);
      h = utils::HashCombine(h, info.minFilter);
      h = utils::HashCombine(h, info.mipmapMode);
      h = utils::HashCombine(h, info.addressModeU);
      h = utils::HashCombine(h, info.addressModeV);
      h = utils::HashCombine(h, info.addressModeW);
      h = utils::HashCombine(h, info.mipLodBias);
      h = utils::HashCombine(h, info.anisotropyEnable);
      h = utils::HashCombine(h, info.maxAnisotropy);
      h = utils::HashCombine(h, info.compareEnable);
      h = utils::HashCombine(h, info.compareOp);
      h = utils::HashCombine(h, info.minLod);
      h = utils::HashCombine(h, info.maxLod);
      h = utils::HashCombine(h, info.borderColor);
      h = utils::HashCombine(h, info.unnormalizedCoordinates);
      return h;
    }
  };

  struct InfoKeyEqual {
    bool operator()(VkSamplerCreateInfo const& lhs, VkSamplerCreateInfo const& rhs) const {
      return memcmp(&lhs, &rhs, sizeof(VkSamplerCreateInfo)) == 0;
    }
  };

  [[nodiscard]]
  VkSampler createSampler(VkSamplerCreateInfo info) const {
    LOG_CHECK( device_ != VK_NULL_HANDLE );
    VkSampler sampler{};
    info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    CHECK_VK( vkCreateSampler(device_, &info, nullptr, &sampler) );
    return sampler;
  }

 private:
  VkDevice device_{};
  VkSampler default_sampler_{};

  mutable std::unordered_map<VkSamplerCreateInfo, VkSampler, InfoHash, InfoKeyEqual> map_{};
};

/* -------------------------------------------------------------------------- */

#endif //  AER_RENDERER_SAMPLER_POOL_H_
