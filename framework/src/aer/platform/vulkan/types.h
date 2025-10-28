#ifndef AER_PLATFORM_VULKAN_TYPES_H_
#define AER_PLATFORM_VULKAN_TYPES_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include <map>

#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#ifdef VMA_IMPLEMENTATION
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wuseless-cast"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wundef"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wundef"
#else
#pragma warning(push)
#pragma warning(disable : 4100)  // Unreferenced formal parameter
#pragma warning(disable : 4189)  // Local variable is initialized but not referenced
#pragma warning(disable : 4127)  // Conditional expression is constant
#pragma warning(disable : 4324)  // Structure was padded due to alignment specifier
#pragma warning(disable : 4505)  // Unreferenced function with internal linkage has been removed
#endif
#endif // VMA_IMPLEMENTATION

#include "vk_mem_alloc.h"

#ifdef VMA_IMPLEMENTATION
#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma warning(pop)
#endif
#endif // VMA_IMPLEMENTATION

namespace backend {

/* -------------------------------------------------------------------------- */
// Resource Allocator

struct Resource {
  bool valid() const noexcept { return false; }
};

struct Image : Resource {
  VkImage image{};
  VkImageView view{};
  VkFormat format{};
  VmaAllocation allocation{};

  bool valid() const noexcept {
    return image != VK_NULL_HANDLE;
  }
};

struct Buffer : Resource {
  VkBuffer buffer{};
  VmaAllocation allocation{};
  VkDeviceAddress address{};

  bool valid() const noexcept {
    return buffer != VK_NULL_HANDLE;
  }
};

// ----------------------------------------------------------------------------
// Context

struct GPUProperties {
  VkPhysicalDeviceProperties2 gpu2{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
  };
  VkPhysicalDeviceMemoryProperties2 memory2{
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2
  };
  std::vector<VkQueueFamilyProperties2> queue_families2{};

  uint32_t get_memory_type_index(
    uint32_t type_bits,
    VkMemoryPropertyFlags requirements_mask
  ) const {
    for (uint32_t i = 0u; i < 32u; ++i) {
      if (type_bits & 1u) {
        auto const props = memory2.memoryProperties.memoryTypes[i].propertyFlags;
        if (requirements_mask == (props & requirements_mask)) {
          return i;
        }
      }
      type_bits >>= 1u;
    }
    return UINT32_MAX;
  }
};

struct Queue {
  VkQueue queue{};
  uint32_t family_index{UINT32_MAX};
  uint32_t queue_index{UINT32_MAX};
};

// ----------------------------------------------------------------------------
// Shader

struct ShaderModule {
  VkShaderModule module{};
  std::string basename{};
};

enum class ShaderStage {
  Vertex        ,
  Fragment      ,
  Compute       ,
  Raygen        ,
  AnyHit        ,
  ClosestHit    ,
  Miss          ,
  Intersection  ,
  Callable      ,
  kCount,
};

using ShaderMap = std::map<ShaderStage, ShaderModule>;
using ShadersMap = std::map<ShaderStage, std::vector<ShaderModule>>;

// ----------------------------------------------------------------------------
// Pipeline

class PipelineInterface {
 public:
  PipelineInterface() = default;

  PipelineInterface(
    VkPipelineLayout layout,
    VkPipeline pipeline,
    VkPipelineBindPoint bind_point
  ) : pipeline_layout_(layout)
    , pipeline_(pipeline)
    , bind_point_(bind_point)
  {}

  virtual ~PipelineInterface() = default;

  VkPipelineLayout layout() const {
    return pipeline_layout_;
  }

  VkPipeline handle() const {
    return pipeline_;
  }

  VkPipelineBindPoint bind_point() const {
    return bind_point_;
  }

 protected:
  VkPipelineLayout pipeline_layout_{}; //
  VkPipeline pipeline_{};
  VkPipelineBindPoint bind_point_{};
};

// ----------------------------------------------------------------------------
// RayTracing

struct RayTracingAddressRegion {
  VkStridedDeviceAddressRegionKHR raygen{};
  VkStridedDeviceAddressRegionKHR miss{};
  VkStridedDeviceAddressRegionKHR hit{};
  VkStridedDeviceAddressRegionKHR callable{};
};

// ----------------------------------------------------------------------------

/* Interface for dynamic rendering. */
struct RTInterface {
  RTInterface() = default;

  virtual ~RTInterface() {}

  // -- Getters --

  virtual VkExtent2D surface_size() const = 0;

  virtual uint32_t color_attachment_count() const = 0;

  virtual std::vector<backend::Image> color_attachments() const = 0;

  virtual backend::Image color_attachment(uint32_t i = 0u) const = 0;

  virtual backend::Image depth_stencil_attachment() const = 0;

  virtual VkClearValue color_clear_value(uint32_t i = 0u) const = 0;

  virtual VkClearValue depth_stencil_clear_value() const = 0;

  virtual VkAttachmentLoadOp color_load_op(uint32_t i = 0u) const = 0;

  virtual uint32_t view_mask() const noexcept = 0;

  virtual uint32_t layer_count() const noexcept {
    return (view_mask() > 0) ? 2u : 1u;
  }

  virtual VkSampleCountFlagBits sample_count() const noexcept = 0;

  virtual std::vector<backend::Image> resolve_attachments() const noexcept = 0;

  virtual backend::Image resolve_attachment(uint32_t i = 0u) const noexcept = 0;

  bool use_msaa() const noexcept {
    return sample_count() > VK_SAMPLE_COUNT_1_BIT;
  }

  // -- Setters --

  virtual void set_color_clear_value(VkClearColorValue clear_color, uint32_t i = 0u) = 0;

  virtual void set_depth_stencil_clear_value(VkClearDepthStencilValue clear_depth_stencil) = 0;

  virtual void set_color_load_op(VkAttachmentLoadOp load_op, uint32_t i = 0u) = 0;

  virtual bool resize(uint32_t w, uint32_t h) = 0;
};

// ----------------------------------------------------------------------------

/* Interface for legacy rendering, via RenderPass and Framebuffer. */
struct RPInterface {
  RPInterface() = default;

  virtual ~RPInterface() {}

  virtual VkRenderPass render_pass() const = 0;

  virtual VkFramebuffer swap_attachment() const = 0;

  virtual VkExtent2D surface_size() const = 0;

  virtual std::vector<VkClearValue> const& clear_values() const = 0;
};

} // namespace "backend"

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

// [might be moved elsewhere, probably Renderer]

struct RenderPassDescriptor {
  std::vector<VkRenderingAttachmentInfo> colorAttachments{};
  VkRenderingAttachmentInfo depthAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
  VkRenderingAttachmentInfo stencilAttachment{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR};
  VkRect2D renderArea{};
  uint32_t viewMask{};
};

struct DescriptorSetLayoutParams {
  uint32_t binding{};
  VkDescriptorType descriptorType{};
  uint32_t descriptorCount{};
  VkShaderStageFlags stageFlags{};
  VkSampler const* pImmutableSamplers{};
  VkDescriptorBindingFlags bindingFlags{};
};
using DescriptorSetLayoutParamsBuffer = std::vector<DescriptorSetLayoutParams>;

struct DescriptorSetWriteEntry {
  uint32_t binding{};
  VkDescriptorType type{};
  std::vector<VkDescriptorImageInfo> images{};
  std::vector<VkDescriptorBufferInfo> buffers{};
  std::vector<VkBufferView> bufferViews{};
  std::vector<VkAccelerationStructureKHR> accelerationStructures{};

  struct Extensions {
    VkWriteDescriptorSetAccelerationStructureKHR accelerationStructureInfo{};
  };

  struct Result {
    Extensions ext{};
    std::vector<VkWriteDescriptorSet> write_descriptor_sets{};
  };
};

struct VertexInputDescriptor {
  std::vector<VkVertexInputBindingDescription2EXT> bindings{};
  std::vector<VkVertexInputAttributeDescription2EXT> attributes{};
  std::vector<uint64_t> vertexBufferOffsets{};
};

/* [WIP] generic requirements to draw something. */
struct DrawDescriptor {
  VertexInputDescriptor vertexInput{};

  //VkPrimitiveTopology topology{};
  VkIndexType indexType{};

  uint64_t indexOffset{};
  uint64_t vertexOffset{};

  uint32_t indexCount{};
  uint32_t vertexCount{};
  uint32_t instanceCount{1u};
};

/* -------------------------------------------------------------------------- */

#endif
