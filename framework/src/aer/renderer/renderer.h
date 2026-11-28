#ifndef AER_RENDERER_RENDERER_H_
#define AER_RENDERER_RENDERER_H_

/* -------------------------------------------------------------------------- */

#include <string_view>

#include "aer/core/common.h"
#include "aer/core/utils.h"

#include "aer/platform/vulkan/swapchain.h"
#include "aer/platform/vulkan/command_encoder.h"

#include "aer/scene/load_options.h"
#include "aer/scene/ecs/animation_system.h"
#include "aer/scene/ecs/physics_system.h"
#include "aer/scene/ecs/hierarchy.h"

#include "aer/renderer/render_context.h"
#include "aer/renderer/gpu_pool.h"
#include "aer/renderer/renderer_flags.h"
#include "aer/renderer/scene_renderer.h"
#include "aer/renderer/ray_tracing_view.h"
#include "aer/renderer/debug_draw.h"
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

  void update(Camera const& camera, float elapsed_time);

  void render(RenderPassEncoder const& pass);

  /* Blit an image to the final default color image, before the swapchain. */
  void blitColor(
    CommandEncoder const& cmd,
    backend::Image const& src_image,
    VkImageLayout src_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
  ) const noexcept;

  // -- Ray tracing --

  /* Record current frame's acceleration structures. */
  void buildAccelerationStructures(CommandEncoder const& cmd);

  // -- Scene --

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

 public:
  // --- Getters ---

  [[nodiscard]]
  RenderContext const& context() const noexcept { return *context_ptr_; }

  [[nodiscard]]
  SwapchainInterface& swapchain() const { return default_frame_handler_.swapchain(); }

  [[nodiscard]]
  backend::Image swapchain_image() const { return swapchain().current_image(); } //

  [[nodiscard]]
  uint32_t swapchain_image_count() const { return swapchain().image_count(); } //

  [[nodiscard]]
  uint32_t frames_in_flight() const noexcept { return default_frame_handler_.frames_in_flight(); }

  [[nodiscard]]
  uint32_t frame_slot() const noexcept { return default_frame_handler_.index; }

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

  [[nodiscard]]
  Skybox& skybox() noexcept { return skybox_; }

  [[nodiscard]]
  float exposure() const noexcept { return exposure_ev_; }

  [[nodiscard]]
  Tonemapping tonemapping() const noexcept { return tonemapping_; }

  [[nodiscard]]
  GpuPool& gpu_pool() noexcept { return gpu_pool_; }

  [[nodiscard]]
  GpuPool const& gpu_pool() const noexcept { return gpu_pool_; }

  [[nodiscard]]
  scene::Hierarchy& scene() noexcept { return scene_tree_; }

  [[nodiscard]]
  scene::Hierarchy const& scene() const noexcept { return scene_tree_; }

  [[nodiscard]]
  scene::AnimationSystem& animation() noexcept { return animation_system_; }

  [[nodiscard]]
  scene::PhysicsSystem& physics() noexcept { return physics_system_; }

  [[nodiscard]]
  DebugDraw& debug_draw() noexcept { return debug_draw_; }

  /* Data to trace the current frame (invalid if nothing can be traced). */
  [[nodiscard]]
  RayTracingView ray_tracing_view() const;

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

  void set_rasterization_enabled(bool enabled); //

  /* Display transform applied to the lighting (exposure in EV stops, 0 = neutral). */
  void set_exposure(float ev) noexcept { exposure_ev_ = ev; }

  void set_tonemapping(Tonemapping tonemapping) noexcept { tonemapping_ = tonemapping; }

  bool setRayTracingEnabled(bool enabled); //

  /* Load an HDR environment (initializes the skybox on first call). */
  bool setSkybox(std::string_view hdr_filename, float hdr_intensity = 1.0f);

 private:
  void initViewResources();

  void releaseViewResources();

  void updatePhysicsDebugDraw(Camera const& camera);

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

  /* Display transform (the swapchain blit does the sRGB encoding). */
  float exposure_ev_{0.0f};
  Tonemapping tonemapping_{Tonemapping::PBRNeutral};

  // --- Scene ---

  /* Scene shaders + materials parameters */
  MaterialFxRegistry material_fx_registry_{};

  /* Scene device memory: vertex/index buffers, images + texture array, .. */
  GpuPool gpu_pool_{};

  // [integrate to Hierarchy ?]
  // -----------------
  /* Scene hierarchy */
  scene::Hierarchy scene_tree_{};

  /* Scene skeletal animations */
  scene::AnimationSystem animation_system_{};

  /* Scene rigid body physics */
  scene::PhysicsSystem physics_system_{};
  // -----------------

  /* Rendering subsystems */
  SceneRenderer scene_renderer_{};

  /* Immediate mode debug draw helper */
  DebugDraw debug_draw_{};

  // --- miscs ---
  Skybox skybox_{};
  bool skybox_initialized_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_RENDERER_H_
