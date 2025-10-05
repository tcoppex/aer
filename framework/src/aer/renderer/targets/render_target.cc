#include "aer/renderer/targets/render_target.h"

#include "aer/platform/backend/context.h"
#include "aer/platform/backend/allocator.h"

/* -------------------------------------------------------------------------- */

RenderTarget::RenderTarget(Context const& context)
  : context_ptr_(&context)
{}

// ----------------------------------------------------------------------------

void RenderTarget::setup(Descriptor_t const& desc) {
  uint32_t const color_count{ static_cast<uint32_t>(desc.color_formats.size()) };
  colors_.resize(color_count);
  color_clear_values_.resize(color_count);
  color_load_ops_.resize(color_count, VK_ATTACHMENT_LOAD_OP_CLEAR);

  for (uint32_t i = 0u; i < color_count; ++i) {
    colors_[i].format = desc.color_formats[i];
    color_clear_values_[i] = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
  }
  depth_stencil_.format = desc.depth_stencil_format;
  depth_stencil_clear_value_ = {{{1.0f, 0u}}};

  // Reset size.
  extent_ = {};
  resize(desc.size.width, desc.size.height);
}

// ----------------------------------------------------------------------------

void RenderTarget::release() {
  LOG_CHECK(context_ptr_ != nullptr);

  auto allocator = context_ptr_->allocator();
  allocator.destroy_image(&depth_stencil_);
  for(auto& color : colors_) {
    allocator.destroy_image(&color);
  }
}

// ----------------------------------------------------------------------------

bool RenderTarget::resize(uint32_t w, uint32_t h) {
  if ((w == extent_.width)
   && (h == extent_.height)) {
    return false;
  }
  release();

  extent_ = {
    .width = w,
    .height = h
  };

  /* Create color images. */
  for (auto &color : colors_) {
    color = context_ptr_->create_image_2d(
      extent_.width, extent_.height, color.format, kDefaultImageUsageFlags
    );
  }
  // context_ptr_->transition_images_layout(colors_, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

  // [TODO] Clear color images via vkCmdClearColorImage ?

  /* Create an optional depth-stencil buffer. */
  if (depth_stencil_.format != VK_FORMAT_UNDEFINED) {
    depth_stencil_ = context_ptr_->create_image_2d(
      extent_.width,
      extent_.height,
      depth_stencil_.format,
        VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      "RenderTarget::DepthStencil"
    );
  }

  return true;
}

/* -------------------------------------------------------------------------- */
