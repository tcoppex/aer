#ifndef AER_RENDERER_TARGETS_RENDER_TARGET_H_
#define AER_RENDERER_TARGETS_RENDER_TARGET_H_

/* -------------------------------------------------------------------------- */

#include "aer/platform/vulkan/types.h"
class Context;

/* -------------------------------------------------------------------------- */

/**
 * RenderTarget are used for dynamic rendering (requires Vulkan 1.3,
 * or 1.1 with extenions).
 *
 * Can only be instantiated by 'Renderer'.
 *
 * A RenderTarget hold N color buffers + 1 optionnal depthStencil buffer.
 * If MSAA is enabled it will create N complementary resolve buffers.
 *
 **/
class RenderTarget : public backend::RTInterface {
 public:
  static constexpr VkImageUsageFlags kDefaultColorImageUsageFlags{
      VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_STORAGE_BIT
    | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    | VK_IMAGE_USAGE_TRANSFER_DST_BIT
  };

  struct Descriptor {
    struct ImageDesc {
      VkFormat format{VK_FORMAT_UNDEFINED};
      VkClearValue clear_value{{{1.0f, 0.0f, 1.0f, 1.0f}}};
      VkAttachmentLoadOp load_op{VK_ATTACHMENT_LOAD_OP_CLEAR};
    };
    std::vector<ImageDesc> colors{};
    ImageDesc depth_stencil{};
    VkExtent2D size{};
    uint32_t array_size{1u};
    VkSampleCountFlagBits sample_count{VK_SAMPLE_COUNT_1_BIT};
    std::string debug_prefix{"RenderTarget"};
  };

 public:
  ~RenderTarget() = default;

  void setup(Descriptor const& desc);

  void release();

 public:
  // ----- RTInterface Overrides -----

  [[nodiscard]]
  VkExtent2D surface_size() const final {
    return surface_size_;
  }

  [[nodiscard]]
  uint32_t color_attachment_count() const final {
    return static_cast<uint32_t>(colors_.size());
  }

  [[nodiscard]]
  std::vector<backend::Image> color_attachments() const final {
    return colors_;
  }

  [[nodiscard]]
  backend::Image color_attachment(uint32_t i = 0u) const final {
    return colors_[i];
  }

  [[nodiscard]]
  std::vector<backend::Image> resolve_attachments() const noexcept final {
    return use_msaa() ? resolves_ : colors_;
  }

  [[nodiscard]]
  backend::Image resolve_attachment(uint32_t i = 0u) const noexcept final {
    return use_msaa() ? resolves_[i] : colors_[i];
  }

  [[nodiscard]]
  backend::Image depth_stencil_attachment() const final {
    return depth_stencil_;
  }

  [[nodiscard]]
  VkClearValue color_clear_value(uint32_t i = 0u) const final {
    return desc_.colors[i].clear_value;
  }

  [[nodiscard]]
  VkClearValue depth_stencil_clear_value() const final {
    return desc_.depth_stencil.clear_value;
  }

  [[nodiscard]]
  VkAttachmentLoadOp color_load_op(uint32_t i = 0u) const final {
    return desc_.colors[i].load_op;
  }

  // ---------------------------
  [[nodiscard]]
  uint32_t view_mask() const noexcept final {
    return (desc_.array_size > 1u) ? (1 << desc_.array_size) - 1
                                   : 0b0u
                                   ;
  }

  [[nodiscard]]
  uint32_t layer_count() const noexcept final {
    return desc_.array_size;
  }

  [[nodiscard]]
  VkSampleCountFlagBits sample_count() const noexcept final {
    return desc_.sample_count;
  }
  // ---------------------------

  void set_color_clear_value(VkClearColorValue value, uint32_t i = 0u) final {
    desc_.colors[i].clear_value.color = value;
  }

  void set_depth_stencil_clear_value(VkClearDepthStencilValue value) final {
    desc_.depth_stencil.clear_value.depthStencil = value;
  }

  void set_color_load_op(VkAttachmentLoadOp load_op, uint32_t i = 0u) final {
    desc_.colors[i].load_op = load_op;
  }

  bool resize(uint32_t w, uint32_t h) final; //

 private:
  RenderTarget(Context const& context);

 private:
  Context const* context_ptr_{};

  Descriptor desc_{};
  VkExtent2D surface_size_{};

  std::vector<backend::Image> colors_{};
  std::vector<backend::Image> resolves_{};
  backend::Image depth_stencil_{};

 private:
  friend class RenderContext;
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_TARGETS_RENDER_TARGET_H_
