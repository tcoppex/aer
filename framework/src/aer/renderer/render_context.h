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
  std::unique_ptr<RenderTarget> create_render_target() const;

  [[nodiscard]]
  std::unique_ptr<RenderTarget> create_render_target(
    RenderTarget::Descriptor const& desc
  ) const;

  // --- Framebuffer (Legacy Rendering) ---

  [[nodiscard]]
  std::unique_ptr<Framebuffer> create_framebuffer(
    SwapchainInterface const& swapchain
  ) const;

  [[nodiscard]]
  std::unique_ptr<Framebuffer> create_framebuffer(
    SwapchainInterface const& swapchain,
    Framebuffer::Descriptor_t const& desc
  ) const;

  // --- Pipeline Layout ---

  [[nodiscard]]
  VkPipelineLayout create_pipeline_layout(
    PipelineLayoutDescriptor_t const& params
  ) const;

  void destroy_pipeline_layout(
    VkPipelineLayout layout
  ) const;

  // --- Pipelines ---

  void destroy_pipeline(
    Pipeline const& pipeline
  ) const;

  // --- Graphics Pipelines ---

  [[nodiscard]]
  VkGraphicsPipelineCreateInfo create_graphics_pipeline_create_info(
    GraphicsPipelineCreateInfoData_t &data,
    VkPipelineLayout pipeline_layout,
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // Batch create graphics pipelines from a common layout.
  void create_graphics_pipelines(
    VkPipelineLayout pipeline_layout,
    std::vector<GraphicsPipelineDescriptor_t> const& descs,
    std::vector<Pipeline> *out_pipelines
  ) const;

  // Create a graphics pipeline with a pre-defined layout.
  [[nodiscard]]
  Pipeline create_graphics_pipeline(
    VkPipelineLayout pipeline_layout,
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // Create a graphics pipeline and a layout based on description.
  [[nodiscard]]
  Pipeline create_graphics_pipeline(
    PipelineLayoutDescriptor_t const& layout_desc,
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // Create a graphics pipeline with a default empty layout.
  [[nodiscard]]
  Pipeline create_graphics_pipeline(
    GraphicsPipelineDescriptor_t const& desc
  ) const;

  // --- Compute Pipelines ---

  void create_compute_pipelines(
    VkPipelineLayout pipeline_layout,
    std::vector<backend::ShaderModule> const& modules,
    Pipeline *pipelines
  ) const;

  [[nodiscard]]
  Pipeline create_compute_pipeline(
    VkPipelineLayout pipeline_layout,
    backend::ShaderModule const& module
  ) const;

  // --- Ray Tracing Pipelines ---

  [[nodiscard]]
  Pipeline create_raytracing_pipeline(
    VkPipelineLayout pipeline_layout,
    RayTracingPipelineDescriptor_t const& desc
  ) const;

  // --- Descriptor Set Registry ---

  [[nodiscard]]
  DescriptorSetRegistry const& descriptor_set_registry() const noexcept {
    return descriptor_set_registry_;
  }

  [[nodiscard]]
  VkDescriptorSetLayout create_descriptor_set_layout(
    DescriptorSetLayoutParamsBuffer const& params,
    VkDescriptorSetLayoutCreateFlags flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
  ) const;

  void destroy_descriptor_set_layout(VkDescriptorSetLayout& layout) const;

  [[nodiscard]]
  VkDescriptorSet create_descriptor_set(VkDescriptorSetLayout const layout) const;

  [[nodiscard]]
  VkDescriptorSet create_descriptor_set(
    VkDescriptorSetLayout const layout,
    std::vector<DescriptorSetWriteEntry> const& entries
  ) const;

  // --- Texture ---

  [[nodiscard]]
  bool load_image_2d(
    CommandEncoder const& cmd,
    std::string_view filename,
    backend::Image& image
  ) const;

  [[nodiscard]]
  bool load_image_2d(
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

 public:
  template <typename... VulkanHandles>
  void destroyResources(VulkanHandles... handles) const {
    (destroyResource(handles), ...);
  }
  void destroyResource(VkDescriptorSetLayout h) const        { destroy_descriptor_set_layout(h); }
  void destroyResource(VkPipelineLayout h) const             { destroy_pipeline_layout(h); }
  void destroyResource(Pipeline const& h) const              { destroy_pipeline(h); }
  void destroyResource(backend::Buffer const& buffer) const  { destroy_buffer(buffer); }
  void destroyResource(backend::Image & image) const         { destroy_image(image); }

 private:
  Settings settings_{};

  VkPipelineCache pipeline_cache_{};
  uint32_t default_view_mask_{};

  SamplerPool sampler_pool_{};
  DescriptorSetRegistry descriptor_set_registry_{};
};

/* -------------------------------------------------------------------------- */

#endif
