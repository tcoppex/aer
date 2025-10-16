#ifndef AER_RENDERER_RENDER_CONTEXT_H_
#define AER_RENDERER_RENDER_CONTEXT_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/vulkan/context.h"

#include "aer/renderer/targets/framebuffer.h"
#include "aer/renderer/targets/render_target.h"
#include "aer/renderer/pipeline.h"
#include "aer/renderer/sampler_pool.h"
#include "aer/renderer/descriptor_set_registry.h" //

class SwapchainInterface;

/* -------------------------------------------------------------------------- */

///
/// Higher level access to the backend device context.
///
class RenderContext : public Context {
 public:
  static constexpr uint32_t kMaxDescriptorPoolSets{ 256u };

  // Default graphics settings.
  struct Settings {
    VkFormat color_format{VK_FORMAT_UNDEFINED};
    VkFormat depth_stencil_format{VK_FORMAT_UNDEFINED};
    VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT};
  };

 public:
  RenderContext() = default;
  ~RenderContext() = default;

  [[nodiscard]]
  bool init(
    Settings const& settings,
    std::string_view app_name,
    std::vector<char const*> const& instance_extensions,
    XRVulkanInterface *vulkan_xr
  );

  void release();

  // --- Render Target (Dynamic Rendering) ---

  [[nodiscard]]
  std::unique_ptr<RenderTarget> createRenderTarget() const;

  [[nodiscard]]
  std::unique_ptr<RenderTarget> createRenderTarget(
    RenderTarget::Descriptor const& desc
  ) const;

  [[nodiscard]]
  std::unique_ptr<RenderTarget> createDefaultRenderTarget() const;

  // --- Framebuffer (Legacy Rendering) ---

  [[nodiscard]]
  std::unique_ptr<Framebuffer> createFramebuffer(
    SwapchainInterface const& swapchain
  ) const;

  [[nodiscard]]
  std::unique_ptr<Framebuffer> createFramebuffer(
    SwapchainInterface const& swapchain,
    Framebuffer::Descriptor_t const& desc
  ) const;

  // --- Pipeline Layout ---

  [[nodiscard]]
  VkPipelineLayout createPipelineLayout(
    PipelineLayoutDescriptor_t const& params
  ) const;

  void destroyPipelineLayout(
    VkPipelineLayout layout
  ) const;

  // --- Pipelines ---

  void destroyPipeline(
    Pipeline const& pipeline
  ) const;

  // --- Graphics Pipelines ---

  [[nodiscard]]
  VkGraphicsPipelineCreateInfo buildGraphicsPipelineCreateInfo(
    GraphicsPipelineCreateInfoData_t &data,
    VkPipelineLayout pipeline_layout,
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // Batch create graphics pipelines from a common layout.
  void createGraphicsPipelines(
    VkPipelineLayout pipeline_layout,
    std::vector<GraphicsPipelineDescriptor_t> const& descs,
    std::vector<Pipeline> *out_pipelines
  ) const;

  // Create a graphics pipeline with a pre-defined layout.
  [[nodiscard]]
  Pipeline createGraphicsPipeline(
    VkPipelineLayout pipeline_layout,
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // Create a graphics pipeline and a layout based on description.
  [[nodiscard]]
  Pipeline createGraphicsPipeline(
    PipelineLayoutDescriptor_t const& layout_desc,
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // Create a graphics pipeline with a default empty layout.
  [[nodiscard]]
  Pipeline createGraphicsPipeline(
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // --- Compute Pipelines ---

  void create_compute_pipelines(
    VkPipelineLayout pipeline_layout,
    std::vector<backend::ShaderModule> const& modules,
    Pipeline *pipelines
  ) const;

  [[nodiscard]]
  Pipeline createComputePipeline(
    VkPipelineLayout pipeline_layout,
    backend::ShaderModule const& module
  ) const;

  // --- Ray Tracing Pipelines ---

  [[nodiscard]]
  Pipeline createRayTracingPipeline(
    VkPipelineLayout pipeline_layout,
    RayTracingPipelineDescriptor_t const& desc
  ) const;

  // --- Descriptor Set Registry ---

  [[nodiscard]]
  DescriptorSetRegistry const& descriptor_set_registry() const noexcept {
    return descriptor_set_registry_;
  }

  [[nodiscard]]
  VkDescriptorSetLayout createDescriptorSetLayout(
    DescriptorSetLayoutParamsBuffer const& params,
    VkDescriptorSetLayoutCreateFlags flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
  ) const;

  void destroyDescriptorSetLayout(VkDescriptorSetLayout& layout) const;

  [[nodiscard]]
  VkDescriptorSet createDescriptorSet(VkDescriptorSetLayout const layout) const;

  [[nodiscard]]
  VkDescriptorSet createDescriptorSet(
    VkDescriptorSetLayout const layout,
    std::vector<DescriptorSetWriteEntry> const& entries
  ) const;

  // --- Texture ---

  [[nodiscard]]
  bool loadImage2D(
    CommandEncoder const& cmd,
    std::string_view filename,
    backend::Image& image
  ) const;

  [[nodiscard]]
  bool loadImage2D(
    std::string_view filename,
    backend::Image& image
  ) const;

  // --- Sampler ---

  [[nodiscard]]
  VkSampler default_sampler() const noexcept {
    return sampler_pool_.default_sampler();
  }

  [[nodiscard]]
  SamplerPool& sampler_pool() noexcept {
    return sampler_pool_;
  }

  [[nodiscard]]
  SamplerPool const& sampler_pool() const noexcept {
    return sampler_pool_;
  }

  // --- Settings ---

  [[nodiscard]]
  VkFormat default_color_format() const noexcept {
    return settings_.color_format;
  }

  [[nodiscard]]
  VkFormat default_depth_stencil_format() const noexcept {
    return settings_.depth_stencil_format;
  }

  [[nodiscard]]
  VkSampleCountFlagBits default_sample_count() const noexcept {
    return settings_.sample_count;
  }

  [[nodiscard]]
  uint32_t default_view_mask() const noexcept {
    return default_view_mask_;
  }

  [[nodiscard]]
  VkExtent2D default_surface_size() const noexcept {
    return default_surface_size_;
  }

  void set_default_surface_size(VkExtent2D const& surface_size) noexcept {
    default_surface_size_ = surface_size;
  }

 public:
  template <typename... VulkanHandles>
  void destroyResources(VulkanHandles... handles) const {
    (destroyResource(handles), ...);
  }
  void destroyResource(VkDescriptorSetLayout h) const        { destroyDescriptorSetLayout(h); }
  void destroyResource(VkPipelineLayout h) const             { destroyPipelineLayout(h); }
  void destroyResource(Pipeline const& h) const              { destroyPipeline(h); }
  void destroyResource(backend::Buffer const& buffer) const  { destroyBuffer(buffer); }
  void destroyResource(backend::Image & image) const         { destroyImage(image); }

 private:
  Settings settings_{};
  uint32_t default_view_mask_{};
  VkExtent2D default_surface_size_{};

  VkPipelineCache pipeline_cache_{};

  SamplerPool sampler_pool_{};
  DescriptorSetRegistry descriptor_set_registry_{};
};

/* -------------------------------------------------------------------------- */

#endif
