#ifndef AER_RENDERER_RENDERER_H_
#define AER_RENDERER_RENDERER_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"

#include "aer/platform/backend/swapchain.h"
// #include "aer/platform/openxr/xr_vulkan_interface.h" //
#include "aer/platform/openxr/openxr_context.h" //

#include "aer/renderer/render_context.h"
#include "aer/platform/backend/command_encoder.h"

#include "aer/renderer/fx/skybox.h"
#include "aer/renderer/gpu_resources.h" // (for GLTFScene)

/* -------------------------------------------------------------------------- */

/**
 * Main entry point to render image to the swapchain and issue commands to the
 * graphic / present queue.
 *
 * -> Use 'begin_frame' / 'end_frame' when ready to submit command via the
 *    returned CommandEncoder object.
 *
 * -> 'create_render_target' can be used to create custom dynamic render target,
 *    'create_framebuffer' to create legacy rendering target.
 *
 **/
class Renderer {
 public:
  static constexpr VkClearValue kDefaultColorClearValue{{
    .float32 = {1.0f, 0.25f, 0.75f, 1.0f}
  }};

 public:
  Renderer() = default;
  ~Renderer() = default;

  void init(
    RenderContext& context,
    SwapchainInterface* swapchain_ptr
  );

  void deinit();

  [[nodiscard]]
  CommandEncoder& begin_frame();

  void end_frame();

  [[nodiscard]]
  RenderContext const& context() const noexcept { return *context_ptr_; }

  [[nodiscard]]
  Skybox const& skybox() const noexcept { return skybox_; }

  [[nodiscard]]
  Skybox& skybox() noexcept { return skybox_; }

  // --- Render Target (Dynamic Rendering) ---

  [[nodiscard]]
  std::unique_ptr<RenderTarget> create_default_render_target(
    uint32_t num_color_outputs = 1u
  ) const;

  // --- Graphics Pipelines ---
  // [those methods are specialization of those found in RenderContext
  // to use internal color/depth buffer by default when unspecified.
  // Ideally it should be remove from here altogether]

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

  // --- GPUResources gltf objects ---

  [[nodiscard]]
  GLTFScene load_gltf(
    std::string_view gltf_filename,
    scene::Mesh::AttributeLocationMap const& attribute_to_location
  );

  [[nodiscard]]
  GLTFScene load_gltf(std::string_view gltf_filename);

  [[nodiscard]]
  std::future<GLTFScene> async_load_gltf(std::string const& filename) {
    return utils::RunTaskGeneric<GLTFScene>([this, filename] {
      return load_gltf(filename);
    });
  }

 public:
  // ------------------------------
  [[nodiscard]]
  VkSampleCountFlagBits sample_count() const noexcept {
    // (some demos used internal fx / images that are setup from this
    // sample count, henceforth we need a way to customize it)
    return VK_SAMPLE_COUNT_4_BIT; //
  }

  [[nodiscard]]
  VkFormat color_format() const noexcept {
    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  }

  [[nodiscard]]
  VkFormat depth_stencil_format() const noexcept {
    return VK_FORMAT_D24_UNORM_S8_UINT;
  }
  // ------------------------------

  [[nodiscard]]
  uint32_t swap_image_count() const noexcept {
    LOG_CHECK(swapchain_ptr_ != nullptr);
    return swapchain_ptr_->imageCount(); //
  }

  [[nodiscard]]
  backend::RTInterface const& main_render_target() const noexcept {
    return *frame_resource().main_rt;
  }

  [[nodiscard]]
  VkExtent2D surface_size() const noexcept {
    return main_render_target().surface_size(); //
  }

  void set_clear_color(vec4 const& color, uint32_t index = 0u) {
    for (auto & frame : frames_) {
      frame.main_rt->set_color_clear_value(
        {.float32 = { color.x, color.y, color.z, color.w }},
        index
      );
    }
  }

  bool resize(uint32_t w, uint32_t h);

  /* Blit an image to the final color image, before the swapchain. */
  void blit(
    CommandEncoder const& cmd,
    backend::Image const& src_image
  ) const noexcept;


 private:
  struct FrameResources {
    VkCommandPool command_pool{};
    VkCommandBuffer command_buffer{};
    CommandEncoder cmd{};
    std::unique_ptr<RenderTarget> main_rt{};
    // std::unique_ptr<RenderTarget> rt_ui{};
  };

  void init_view_resources();

  void deinit_view_resources();

  FrameResources& frame_resource() noexcept {
    return frames_[frame_index_];
  }

  FrameResources const& frame_resource() const noexcept {
    return frames_[frame_index_];
  }

  void apply_postprocess();

 private:
  /* Non owning References. */
  RenderContext* context_ptr_{};
  ResourceAllocator* allocator_ptr_{};
  VkDevice device_{};
  SwapchainInterface* swapchain_ptr_{};

  /* Timeline frame resources */
  std::vector<FrameResources> frames_{};
  uint32_t frame_index_{};

  // ----------

  Skybox skybox_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_RENDERER_H_
