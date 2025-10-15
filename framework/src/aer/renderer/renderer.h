#ifndef AER_RENDERER_RENDERER_H_
#define AER_RENDERER_RENDERER_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"

#include "aer/platform/vulkan/swapchain.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/platform/openxr/openxr_context.h" //

#include "aer/renderer/render_context.h"
#include "aer/renderer/fx/skybox.h"
#include "aer/renderer/gpu_resources.h" // (for GLTFScene)

/* -------------------------------------------------------------------------- */

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
    SwapchainInterface** swapchain_ptr
  );

  void release();

  bool resize(uint32_t w, uint32_t h);

  [[nodiscard]]
  CommandEncoder& begin_frame();

  void end_frame();

  /* Blit an image to the final color image, before the swapchain. */
  void blit_color(
    CommandEncoder const& cmd,
    backend::Image const& src_image
  ) const noexcept;


  // -----------------------------------------------
  // --- Render Target (Dynamic Rendering) ---

  [[nodiscard]]
  std::unique_ptr<RenderTarget> create_default_render_target(
    uint32_t num_color_outputs = 1u
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
  // -----------------------------------------------

  // --- Getters ---

  [[nodiscard]]
  RenderContext const& context() const noexcept {
    return *context_ptr_;
  }

  [[nodiscard]]
  Skybox const& skybox() const noexcept {
    return skybox_;
  }

  [[nodiscard]]
  Skybox& skybox() noexcept {
    return skybox_;
  }

  // ------------------------------
  [[nodiscard]]
  VkFormat color_format() const noexcept {
    return context_ptr_->default_color_format();
  }

  [[nodiscard]]
  VkFormat depth_stencil_format() const noexcept {
    return context_ptr_->default_depth_stencil_format();
  }

  [[nodiscard]]
  VkSampleCountFlagBits sample_count() const noexcept {
    return context_ptr_->default_sample_count();
  }
  // ------------------------------

  [[nodiscard]]
  SwapchainInterface& swapchain() const {
    LOG_CHECK(swapchain_ptr_ != nullptr);
    return **swapchain_ptr_;
  }

  [[nodiscard]]
  uint32_t swapchain_image_count() const {
    return swapchain().imageCount();
  }

  [[nodiscard]]
  backend::Image swapchain_image() const {
    return swapchain().currentImage();
  }

  [[nodiscard]]
  backend::RTInterface const& main_render_target() const noexcept {
    return *frame_resource().main_rt;
  }

  [[nodiscard]]
  VkExtent2D surface_size() const noexcept {
    return main_render_target().surface_size(); //
  }

  // --- Setters ---

  void set_clear_color(vec4 const& color, uint32_t index = 0u) {
    for (auto & frame : frames_) {
      frame.main_rt->set_color_clear_value(
        {.float32 = { color.x, color.y, color.z, color.w }},
        index
      );
    }
  }

  void enable_postprocess(bool status) noexcept {
    enable_postprocess_ = status;
  }

 private:
  struct FrameResources {
    VkCommandPool command_pool{};
    VkCommandBuffer command_buffer{};
    CommandEncoder cmd{};
    std::unique_ptr<RenderTarget> main_rt{};
  };

  void init_view_resources();

  void release_view_resources();

  void apply_postprocess();

  FrameResources& frame_resource() noexcept {
    return frames_[frame_index_];
  }

  FrameResources const& frame_resource() const noexcept {
    return frames_[frame_index_];
  }

 private:
  /* Non owning References. */
  RenderContext* context_ptr_{};
  VkDevice device_{};
  SwapchainInterface** swapchain_ptr_{};

  /* Timeline frame resources */
  std::vector<FrameResources> frames_{};
  uint32_t frame_index_{};

  bool enable_postprocess_{true};

  // ----------

  Skybox skybox_{}; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_RENDERER_H_
