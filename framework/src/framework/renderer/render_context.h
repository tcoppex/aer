#ifndef VKFRAMEWORK_RENDERER_RENDER_CONTEXT_H_
#define VKFRAMEWORK_RENDERER_RENDER_CONTEXT_H_

/* -------------------------------------------------------------------------- */

#include "framework/core/common.h"
#include "framework/backend/context.h"

#include "framework/renderer/targets/framebuffer.h"
#include "framework/renderer/targets/render_target.h"
#include "framework/renderer/pipeline.h"
#include "framework/renderer/sampler_pool.h"
#include "framework/renderer/descriptor_set_registry.h" //

class SwapchainInterface;

/* -------------------------------------------------------------------------- */

///
/// Higher level access to the backend device context.
///
class RenderContext : public Context {
 public:
  static constexpr uint32_t kMaxDescriptorPoolSets{ 256u };

 public:
  RenderContext() = default;
  ~RenderContext() = default;

  [[nodiscard]]
  bool init(
    std::vector<char const*> const& instance_extensions,
    std::vector<char const*> const& device_extensions,
    std::shared_ptr<XRVulkanInterface> vulkan_xr
  );

  void deinit();

  // --- Render Target (Dynamic Rendering) ---

  [[nodiscard]]
  std::unique_ptr<RenderTarget> create_render_target() const;

  [[nodiscard]]
  std::unique_ptr<RenderTarget> create_render_target(
    RenderTarget::Descriptor_t const& desc
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

  // [[nodiscard]]
  // VkGraphicsPipelineCreateInfo create_graphics_pipeline_create_info(
  //   GraphicsPipelineCreateInfoData_t &data,
  //   VkPipelineLayout pipeline_layout,
  //   GraphicsPipelineDescriptor_t const& desc
  // ) const;

  // Batch create graphics pipelines from a common layout.
  void create_graphics_pipelines(
    VkPipelineLayout pipeline_layout,
    std::vector<VkGraphicsPipelineCreateInfo> const& _create_infos,
    std::vector<Pipeline> *out_pipelines
  ) const;

  // Create a graphics pipeline with a pre-defined layout.
  [[nodiscard]]
  Pipeline create_graphics_pipeline(
    VkPipelineLayout pipeline_layout,
    VkGraphicsPipelineCreateInfo const& create_info
  ) const;

  // Create a graphics pipeline and a layout based on description.
  [[nodiscard]]
  Pipeline create_graphics_pipeline(
    PipelineLayoutDescriptor_t const& layout_desc,
    VkGraphicsPipelineCreateInfo const& create_info
  ) const;

  // Create a graphics pipeline with a default empty layout.
  [[nodiscard]]
  Pipeline create_graphics_pipeline(
    VkGraphicsPipelineCreateInfo const& create_info
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

 private:
  VkPipelineCache pipeline_cache_{};
  SamplerPool sampler_pool_{};
  DescriptorSetRegistry descriptor_set_registry_{};
};

/* -------------------------------------------------------------------------- */

#endif
