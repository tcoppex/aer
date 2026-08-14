#pragma once

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"
#include "aer/platform/vulkan/command_encoder.h"

class Context;
class Skybox;
class RayTracingSceneInterface;

/* -------------------------------------------------------------------------- */

// class DescriptorPool {};

// ----------------------------------------------------------------------------

///
/// Handler to access the renderer global Descriptor Sets:
///   - Frame, for dynamic per-frame data (eg. camera matrices)
///   - Scene, for scene shared resources (eg. TextureAtlas, IBL)
///   - RayTracing, for scene data that could change (eg. raytracing instances)
///
class DescriptorSetRegistry {
 private:
  static constexpr uint32_t kMaxNumTextures = 512u; //

 public:
  enum class Type {
    Frame,
    Scene,
    RayTracing,
    kCount,
  };

  struct DescriptorSet {
    uint32_t index{};
    uint32_t binding{};
    VkDescriptorSet set{};
    VkDescriptorSetLayout layout{};
    mutable std::vector<uint32_t> dynamicOffsets{};
  };

 public:
  DescriptorSetRegistry() = default;

  /* Allocate the main DescriptorSets. */
  void init(Context const& context, uint32_t const max_sets);
  
  void release();

  /* Return an internal main DescriptorSet. */
  [[nodiscard]]
  DescriptorSet const& descriptor(Type type) const noexcept {
    return sets_[type];
  };

  /* Methods to allocate custom descriptor set and layout. */

  [[nodiscard]]
  VkDescriptorSetLayout createLayout(
    DescriptorSetLayoutParamsBuffer const& params,
    VkDescriptorSetLayoutCreateFlags flags
  ) const;

  void destroyLayout(VkDescriptorSetLayout &layout) const;

  [[nodiscard]]
  VkDescriptorSet allocateDescriptorSet(
    VkDescriptorSetLayout const layout
  ) const;

  /* Helper to bind internal descriptor sets. */
  void bindDescriptorSet(
    Type type,
    GenericCommandEncoder const& cmd,
    VkPipelineLayout pipeline_layout,
    VkShaderStageFlags const stage_flags
  ) const;

  /* Methods to update shared internal descriptor sets. */
  void updateFrameUBO(backend::Buffer const& buffer) const;
  void updateSceneTransforms(backend::Buffer const& buffer) const;
  void updateSceneTextures(std::vector<VkDescriptorImageInfo> image_infos) const;
  void updateSceneTexture(uint32_t index, VkDescriptorImageInfo image_info) const;
  void updateSceneIBL(Skybox const& skybox) const;
  void updateRayTracingScene(RayTracingSceneInterface const* rt_scene) const;
  // -----------------------------------------------

  void updateFrameUBODynamicOffset(uint32_t offset) const;

 private:
  void initDescriptorPool(uint32_t const max_sets);

  void initDescriptorSets();

  void createMainSet(
    Type const type,
    DescriptorSetLayoutParamsBuffer const& layout_params,
    VkDescriptorSetLayoutCreateFlags layout_flags,
    std::string const& name
  );

 private:
  Context const* context_ptr_{}; //
  VkDevice device_{}; //

  std::vector<VkDescriptorPoolSize> descriptor_pool_sizes_{};
  VkDescriptorPool main_pool_{};

  EnumArray<DescriptorSet, Type> sets_{};
};

/* -------------------------------------------------------------------------- */
