#ifndef AER_RENDERER_TARGETS_FRAMEBUFFER_H_
#define AER_RENDERER_TARGETS_FRAMEBUFFER_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"

class Context;
class SwapchainInterface;

/* -------------------------------------------------------------------------- */

/**
 * Framebuffer are used for legacy rendering setup.
 * It uses VkRenderPass and VkFramebuffer internally.
 *
 * Prefers using RenderTarget / RTInterface with dynamic_rendering instead.
 *
 * Can only be instantiated by 'Renderer'.
 **/
class Framebuffer final : public backend::RPInterface {
 public:
  enum class BufferName {
    Color,
    DepthStencil,
    kCount,
  };

  using SwapBuffer = EnumArray<std::vector<backend::Image>, BufferName>;

  struct Descriptor_t {
    VkExtent2D dimension{};
    VkAttachmentDescription color_desc{};
    VkFormat depth_stencil_format{};
    bool match_swapchain_output_count{};
  };

 public:
  ~Framebuffer() {}

  void release();

  void set_clear_color(VkClearColorValue const& value) {
    clear_values_[static_cast<uint32_t>(BufferName::Color)].color = value;
  }

  void set_clear_depth_stencil(VkClearDepthStencilValue const& value) {
    clear_values_[static_cast<uint32_t>(BufferName::DepthStencil)].depthStencil = value;
  }

  void resize(VkExtent2D const dimension);

  backend::Image color_attachment() const {
    return outputs_[BufferName::Color][get_swap_index()];
  }

  // ----- RPInterface Overrides -----

  VkExtent2D surface_size() const final {
    return desc_.dimension;
  }

  std::vector<VkClearValue> const& clear_values() const final {
    return clear_values_;
  }

  VkRenderPass render_pass() const final {
    return render_pass_;
  }

  VkFramebuffer swap_attachment() const final {
    return framebuffers_[get_swap_index()];
  }

 private:
  Framebuffer(Context const& context, SwapchainInterface const& swapchain);

  void setup(Descriptor_t const& desc);

  void release_buffers();

  uint32_t get_swap_index() const;

 private:
  Context const* context_ptr_{};
  SwapchainInterface const* swapchain_ptr_{};

  Descriptor_t desc_{}; //
  bool use_depth_stencil_{};

  std::vector<VkClearValue> clear_values_{};

  VkRenderPass render_pass_{};
  std::vector<VkFramebuffer> framebuffers_{};
  SwapBuffer outputs_{};

 private:
  friend class RenderContext;
};

/* -------------------------------------------------------------------------- */

#endif