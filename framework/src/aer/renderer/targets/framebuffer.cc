#include "aer/renderer/targets/framebuffer.h"

#include "aer/platform/backend/context.h"
#include "aer/renderer/targets/render_target.h" // (for kDefaultImageUsageFlags)
#include "aer/platform/swapchain_interface.h"

/* -------------------------------------------------------------------------- */

void Framebuffer::release() {
  release_buffers();

  vkDestroyRenderPass(context_ptr_->device(), render_pass_, nullptr);
  render_pass_ = VK_NULL_HANDLE;
}

// ----------------------------------------------------------------------------

void Framebuffer::resize(VkExtent2D const dimension) {
  if ((dimension.width == desc_.dimension.width)
   && (dimension.height == desc_.dimension.height)
   && (framebuffers_[0u] != VK_NULL_HANDLE)) {
    return;
  }

  desc_.dimension = dimension;
  release_buffers();

  // Color(s).
  for (auto& color : outputs_[BufferName::Color]) {
    color = context_ptr_->create_image_2d(
      dimension.width,
      dimension.height,
      desc_.color_desc.format,
      RenderTarget::kDefaultImageUsageFlags //
    );
  }
  context_ptr_->transition_images_layout(
    outputs_[BufferName::Color],
    desc_.color_desc.initialLayout,
    desc_.color_desc.finalLayout
  );

  // DepthStencil(s).
  if (use_depth_stencil_) {
    for (auto& depth_stencil : outputs_[BufferName::DepthStencil]) {
      depth_stencil = context_ptr_->create_image_2d(
        dimension.width,
        dimension.height,
        desc_.depth_stencil_format,
          VK_IMAGE_USAGE_SAMPLED_BIT
        | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
      );
    }
  }

  uint32_t const attachmentCount{
    static_cast<uint32_t>(BufferName::kCount) - (use_depth_stencil_ ? 0u : 1u)
  };
  EnumArray<VkImageView, BufferName> attachments{};
  VkFramebufferCreateInfo const framebuffer_create_info{
    .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
    .renderPass = render_pass_,
    .attachmentCount = attachmentCount,
    .pAttachments = attachments.data(),
    .width = dimension.width,
    .height = dimension.height,
    .layers = 1u,
  };
  for (size_t i = 0u; i < framebuffers_.size(); ++i) {
    attachments[BufferName::Color] = outputs_[BufferName::Color][i].view;
    attachments[BufferName::DepthStencil] = outputs_[BufferName::DepthStencil][i].view;
    CHECK_VK(vkCreateFramebuffer(
      context_ptr_->device(), &framebuffer_create_info, nullptr, &framebuffers_[i]
    ));
  }
}

/* -------------------------------------------------------------------------- */

Framebuffer::Framebuffer(Context const& context, SwapchainInterface const& swapchain)
  : context_ptr_(&context)
  , swapchain_ptr_(&swapchain)
{}

// ----------------------------------------------------------------------------

void Framebuffer::release_buffers() {
  VkDevice const device = context_ptr_->device();
  for (auto &framebuffer : framebuffers_) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
  }

  auto allocator = context_ptr_->allocator();
  for (auto& color : outputs_[BufferName::Color]) {
    allocator.destroy_image(&color);
  }
  for (auto& depth_stencil : outputs_[BufferName::DepthStencil]) {
    allocator.destroy_image(&depth_stencil);
  }
}

// ----------------------------------------------------------------------------

void Framebuffer::setup(Descriptor_t const& desc) {
  desc_ = desc;
  use_depth_stencil_ = (desc_.depth_stencil_format != VK_FORMAT_UNDEFINED);

  clear_values_.resize(static_cast<uint32_t>(BufferName::kCount), {}); //

  /* Resize the buffers depending weither we wanna sync to the current swap index. */
  uint32_t const image_swap_count{
    desc_.match_swapchain_output_count ? swapchain_ptr_->imageCount() : 1u
  };
  framebuffers_.resize(image_swap_count);
  for (auto& output : outputs_) {
    output.resize(image_swap_count);
  }

  /* Setup the render pass. */
  {
    std::vector<VkAttachmentDescription> attachment_descs{desc_.color_desc};
    if (use_depth_stencil_) {
      attachment_descs.push_back(
        {
          .format = desc_.depth_stencil_format,
          .samples = VK_SAMPLE_COUNT_1_BIT,
          .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
          .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, //
          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE, //
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        }
      );
    }
    std::vector<VkAttachmentReference> const references{
      {
        .attachment = static_cast<uint32_t>(BufferName::Color),
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      },
      {
        .attachment = static_cast<uint32_t>(BufferName::DepthStencil),
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      }
    };
    std::vector<VkSubpassDescription> const subpasses{
      {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &references[0u],
        .pDepthStencilAttachment = use_depth_stencil_ ? &references[1u] : nullptr,
      }
    };
    VkRenderPassCreateInfo const render_pass_create_info{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(attachment_descs.size()),
      .pAttachments = attachment_descs.data(),
      .subpassCount = static_cast<uint32_t>(subpasses.size()),
      .pSubpasses = subpasses.data(),
    };
    CHECK_VK(vkCreateRenderPass(context_ptr_->device(), &render_pass_create_info, nullptr, &render_pass_));
  }

  /* Create the framebuffer & attachment images for each outputs. */
  resize(desc_.dimension); //
}

// ----------------------------------------------------------------------------

uint32_t Framebuffer::get_swap_index() const {
  LOGW("{} not implemented", __FUNCTION__);
  return 0;
  // return desc_.match_swapchain_output_count ? swapchain_ptr_->swap_index()
  //                                           : 0u
  //                                           ;
}

/* -------------------------------------------------------------------------- */
