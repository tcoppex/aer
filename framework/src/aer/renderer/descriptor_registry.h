#pragma once

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"
#include "aer/platform/vulkan/command_encoder.h"

class Context;
class Skybox;
class RayTracingSceneInterface;

/* -------------------------------------------------------------------------- */

///
/// Handler to access the renderer global Descriptor Sets:
///   - Frame, for dynamic per-frame data (eg. camera matrices)
///   - Scene, for scene shared resources (eg. TextureAtlas, IBL)
///   - RayTracing, for scene data that could change (eg. raytracing instances)
///
class DescriptorRegistry {
 private:
  static constexpr uint32_t kMaxNumTextures = 1 << 14; // 16384

 public:
  enum class Type {
    Scene,
    RayTracing,
    kCount,
  };

  struct Descriptor {
    uint32_t index{};
    uint32_t binding{};
    VkDescriptorSetLayout layout{};
    // -----
    // (descriptor set resources)
    VkDescriptorSet set{};
    mutable std::vector<uint32_t> dynamicOffsets{};
    // -----
    // (descriptor buffer resources)
    VkDeviceSize layoutSize{};
    VkDeviceSize offset{};
    backend::Buffer buffer{};
  };

 public:
  DescriptorRegistry() = default;

  /* Allocate the main DescriptorSets. */
  void init(Context const& context, uint32_t const max_sets);

  void release();

  /* Return an internal main Descriptor. */
  [[nodiscard]]
  Descriptor const& descriptor(Type type) const noexcept {
    return descriptors_[type];
  };

  /* Methods to allocate custom descriptor set and layout. */
  [[nodiscard]]
  VkDescriptorSetLayout createLayout(
    DescriptorSetLayoutParamsBuffer const& params,
    VkDescriptorSetLayoutCreateFlags flags,
    std::string const& name = ""
  ) const;

  void destroyLayout(VkDescriptorSetLayout &layout) const;

  [[nodiscard]]
  backend::Buffer allocateDescriptorBuffer(
    VkDescriptorSetLayout const layout,
    VkDeviceSize *pLayoutSize,
    VkDeviceSize *pOffset,
    uint32_t num_elems,
    VkBufferUsageFlags2KHR usage_flags,
    std::string const& name = ""
  ) const;

  [[nodiscard]]
  VkDescriptorSet allocateDescriptorSet(
    VkDescriptorSetLayout const layout,
    std::string const& name = ""
  ) const;

  /* Helper to bind internal descriptor sets. */
  void bindDescriptorSet(
    Type type,
    GenericCommandEncoder const& cmd,
    VkPipelineLayout pipeline_layout,
    VkShaderStageFlags const stage_flags
  ) const;


  void updateSceneIBL(Skybox const& skybox) const;
  void updateSceneTextures(std::vector<VkDescriptorImageInfo> image_infos) const;
  void updateSceneTexture(uint32_t index, VkDescriptorImageInfo image_info) const;
  void updateRayTracingScene(RayTracingSceneInterface const* rt_scene) const;

 private:
  void initDescriptorPool(uint32_t const max_sets);

  void setupMainDescriptors();

  [[nodiscard]]
  Descriptor& _intializeMainDescriptor(
    Type const type,
    DescriptorSetLayoutParamsBuffer const& layout_params,
    VkDescriptorSetLayoutCreateFlags layout_flags,
    std::string const& name
  );

  void createMainDescriptorBuffer(
    Type const type,
    DescriptorSetLayoutParamsBuffer const& layout_params,
    VkDescriptorSetLayoutCreateFlags layout_flags,
    uint32_t num_elems,
    VkBufferUsageFlags2KHR usage_flags,
    std::string const& name
  );

  void createMainDescriptorSet(
    Type const type,
    DescriptorSetLayoutParamsBuffer const& layout_params,
    VkDescriptorSetLayoutCreateFlags layout_flags,
    std::string const& name
  );

 private:
  Context const* context_ptr_{};
  VkDevice device_{};

  std::vector<VkDescriptorPoolSize> descriptor_pool_sizes_{};
  VkDescriptorPool main_pool_{};

  EnumArray<Descriptor, Type> descriptors_{};
};

/* -------------------------------------------------------------------------- */
