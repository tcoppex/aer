#include "aer/renderer/targets/render_target.h"

#include "aer/platform/backend/context.h"
#include "aer/platform/backend/allocator.h"

/* -------------------------------------------------------------------------- */

RenderTarget::RenderTarget(Context const& context)
  : context_ptr_(&context)
{}

// ----------------------------------------------------------------------------

void RenderTarget::setup(Descriptor const& desc) {
  desc_ = desc;
  surface_size_ = {};
  colors_.resize(desc.colors.size());
  resolves_.resize(desc.colors.size());
  resize(desc.size.width, desc.size.height);
}

// ----------------------------------------------------------------------------

void RenderTarget::release() {
  LOG_CHECK(context_ptr_ != nullptr);

  auto allocator = context_ptr_->allocator();
  allocator.destroy_image(&depth_stencil_);
  for(auto& resolve : resolves_) {
    allocator.destroy_image(&resolve);
  }
  for(auto& color : colors_) {
    allocator.destroy_image(&color);
  }
}

// ----------------------------------------------------------------------------

bool RenderTarget::resize(uint32_t w, uint32_t h) {
  if ((w == surface_size_.width)
   && (h == surface_size_.height)) {
    return false;
  }
  release();

  surface_size_ = {
    .width = w,
    .height = h,
  };
  uint32_t const levels = 1u; //

  /* Create color images. */
  auto colorUsages = use_msaa() ? VkImageUsageFlags(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT)
                                : kDefaultColorImageUsageFlags
                                ;
  for (size_t i = 0; i < colors_.size(); ++i) {
    colors_[i] = context_ptr_->create_image_2d(
      surface_size_.width,
      surface_size_.height,
      desc_.array_size,
      levels,
      desc_.colors[i].format,
      desc_.sample_count,
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | colorUsages,
      desc_.debug_prefix + "::Color" + std::to_string(i)
    );
  }

  /* When using MSAA we need to allocate additional resolve buffers. */
  if (use_msaa()) {
    LOG_CHECK(resolves_.size() == colors_.size());
    for (size_t i = 0; i < resolves_.size(); ++i) {
      resolves_[i] = context_ptr_->create_image_2d(
        surface_size_.width,
        surface_size_.height,
        desc_.array_size,
        levels,
        desc_.colors[i].format,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | kDefaultColorImageUsageFlags,
        desc_.debug_prefix + "::ResolveColor" + std::to_string(i)
      );
    }

    context_ptr_->transition_images_layout(
      resolves_,
      VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
  }

  /* Create an optional depth-stencil buffer. */
  if (desc_.depth_stencil.format != VK_FORMAT_UNDEFINED) {
    auto depthStencilUsage = use_msaa() ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT
                                        : VK_IMAGE_USAGE_SAMPLED_BIT
                                        ;
    depth_stencil_ = context_ptr_->create_image_2d(
      surface_size_.width,
      surface_size_.height,
      desc_.array_size,
      levels,
      desc_.depth_stencil.format,
      desc_.sample_count,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | depthStencilUsage,
      desc_.debug_prefix + "::DepthStencil"
    );
  }

  return true;
}

/* -------------------------------------------------------------------------- */
