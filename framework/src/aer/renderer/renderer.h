#ifndef AER_RENDERER_RENDERER_H_
#define AER_RENDERER_RENDERER_H_

/* -------------------------------------------------------------------------- */

#include <string_view>

#include "aer/core/common.h"
#include "aer/core/utils.h"

#include "aer/platform/vulkan/swapchain.h"
#include "aer/platform/vulkan/command_encoder.h"

#include "aer/scene/load_options.h"
#include "aer/scene/ecs/hierarchy.h"

#include "aer/renderer/render_context.h"
#include "aer/renderer/gpu_pool.h"
#include "aer/renderer/scene_renderer.h"
#include "aer/renderer/fx/material/material_fx_registry.h"
#include "aer/renderer/fx/skybox.h" //

/* -------------------------------------------------------------------------- */

//
//    Renderer
//
//  Owns :
//    - frames, targets, and swapchain
//    - internal rendering parameters
//    - (tmp?) global fx techniques
//
class Renderer {
 public:
  static constexpr uint64_t kDefaultPoolVertexCapacity = utils::MegaBytes(512u);
  static constexpr uint64_t kDefaultPoolIndexCapacity  = utils::MegaBytes(64u);

  static constexpr VkClearValue kDefaultColorClearValue{{
    .float32 = {1.0f, 0.25f, 0.75f, 1.0f}
  }};

 public:
  struct FrameHandler {
    SwapchainInterface** swapchain_pptr{};
    FrameResourcesBuffer frames{};
    uint32_t index{};

    [[nodiscard]]
    SwapchainInterface& swapchain() const {
      LOG_CHECK(swapchain_pptr && *swapchain_pptr);
      return **swapchain_pptr;
    }

    [[nodiscard]]
    FrameResources& frame() { return frames[index]; }

    [[nodiscard]]
    FrameResources const& frame() const { return frames[index]; }

    [[nodiscard]]
    uint32_t frames_in_flight() const noexcept { return static_cast<uint32_t>(frames.size()); }

    void swapIndex() { index = (index + 1u) % swapchain().image_count(); }
  };

 public:
  Renderer() = default;
  ~Renderer() = default;

  [[nodiscard]]
  bool init(RenderContext& context, SwapchainInterface** swapchain_ptr);

  void release();

  bool resize(uint32_t w, uint32_t h);

  [[nodiscard]]
  CommandEncoder& beginFrame(FrameHandler *frame_handler = nullptr);

  bool endFrame();

  /* Blit an image to the final default color image, before the swapchain. */
  void blitColor(
    CommandEncoder const& cmd,
    backend::Image const& src_image,
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  ) const noexcept;

 public:
  // --- Getters ---

  [[nodiscard]]
  RenderContext const& context() const noexcept { return *context_ptr_; }

  [[nodiscard]]
  uint32_t frames_in_flight() const noexcept { return default_frame_handler_.frames_in_flight(); }

  [[nodiscard]]
  SwapchainInterface& swapchain() const { return default_frame_handler_.swapchain(); }

  [[nodiscard]]
  uint32_t swapchain_image_count() const { return swapchain().image_count(); } //

  [[nodiscard]]
  backend::Image swapchain_image() const { return swapchain().current_image(); } //

  [[nodiscard]]
  backend::RTInterface const& main_render_target() const noexcept {
    return *(default_frame_handler_.frame().main_rt);
  }

  [[nodiscard]]
  VkExtent2D surface_size() const noexcept { return main_render_target().surface_size(); }

  [[nodiscard]]
  bool is_swapchain_blit_enabled() const noexcept { return swapchain_blit_enabled_; }

  [[nodiscard]]
  bool is_foveated_rendering_enabled() const noexcept { return foveated_rendering_; }

  // [[nodiscard]]
  // Skybox& skybox() noexcept { return skybox_; }

  // --- Setters ---

  void set_clear_color(vec4 const& color) {
    for (auto & frame : default_frame_handler_.frames) {
      frame.main_rt->set_color_clear_value({
        .float32 = { color.x, color.y, color.z, color.w }
      });
    }
  }

  void set_swapchain_blit_enabled(bool enabled) noexcept { swapchain_blit_enabled_ = enabled; }

  void set_foveated_rendering_enabled(bool enabled) noexcept { foveated_rendering_ = enabled; }

 private:
  void initViewResources();

  void releaseViewResources();

  /* Blit the main rt color to the swapchain */
  void blitColorToSwapchain(FrameHandler const& frame_handler);

  [[nodiscard]]
  VkFormat color_format() const noexcept { return context_ptr_->default_color_format(); }

  [[nodiscard]]
  VkFormat depth_stencil_format() const noexcept { return context_ptr_->default_depth_stencil_format(); }

  [[nodiscard]]
  VkSampleCountFlagBits sample_count() const noexcept { return context_ptr_->default_sample_count(); }

 private:
  /* Non owning References. */
  RenderContext* context_ptr_{};


  // --- Frame ---

  /* Internal / Default renderer target. */
  FrameHandler default_frame_handler_{};

  /* Current render pass frame handler for changing target. */
  FrameHandler* binded_frame_handler_{nullptr};


  // --- Rendering States ---

  /* Control whether the RT color should be blit to the swapchain or not. */
  bool swapchain_blit_enabled_{true};

  /* [XR] Control whether to use fragment density map when available. */
  bool foveated_rendering_{true};


  // -----------------------------------------------------------------------
  // -----------------------------------------------------------------------

 public:

  [[nodiscard]]
  AssetHandle loadAsset(std::string_view filename, scene::LoadOptions = {});

  bool releaseAsset(AssetHandle);

  // (rename instantiate?)
  entt::entity createInstance(
    AssetHandle,
    entt::entity parent = entt::null,
    mat4 const& local = lina::identity
  );

  void destroyInstance(entt::entity instance_root);

  entt::entity load(
    std::string_view filename,
    entt::entity parent = entt::null,
    mat4 const& local = lina::identity,
    scene::LoadOptions = {}
  );

  // ----

  // std::future<std::optional<Asset>> asyncLoad(std::string file); // workers
  // AssetHandle commit(std::future<...>&); // main thread

  // ----

  void update(Camera const& camera, float dt) {
    scene_tree_.update();
    scene_renderer_.update(
      scene_tree_, camera, dt, default_frame_handler_.index
    );
  }

  void render(RenderPassEncoder const& pass) {
    scene_renderer_.render(pass);
  }

  // ----

  [[nodiscard]]
  scene::Hierarchy& scene() noexcept { return scene_tree_; }

  [[nodiscard]]
  scene::Hierarchy const& scene() const noexcept { return scene_tree_; }

 private:
  // pipelines + per-model material buffers (made incremental)
  // (pipelines setup copied from GPUResources::upload)
  MaterialFxRegistry material_fx_registry_{};

  // vertex/index buffers, images + texture array, DeviceAsset records
  GpuPool gpu_pool_{};

  // the scene: entities, transforms, Renderable / Instance components
  scene::Hierarchy scene_tree_{};

  // frame SBO, transforms SBO, draw lists, sorting, TLAS, render()
  SceneRenderer scene_renderer_{};

  // --- miscs ---

  // Skybox skybox_{}; //

};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_RENDERER_H_
