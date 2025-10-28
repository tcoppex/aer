#include "aer/platform/vulkan/command_encoder.h"

#include "aer/platform/vulkan/allocator.h"
#include "aer/renderer/fx/postprocess/post_fx_interface.h"

/* -------------------------------------------------------------------------- */

void GenericCommandEncoder::bindDescriptorSet(
  VkDescriptorSet descriptor_set,
  VkPipelineLayout pipeline_layout,
  VkShaderStageFlags stage_flags,
  uint32_t first_set
) const {
  if (vkCmdBindDescriptorSets2KHR)
  {
    // (requires VK_KHR_maintenance6 or VK_VERSION_1_4)
    VkBindDescriptorSetsInfoKHR const bind_desc_sets_info{
      .sType = VK_STRUCTURE_TYPE_BIND_DESCRIPTOR_SETS_INFO_KHR,
      .stageFlags = stage_flags,
      .layout = pipeline_layout,
      .firstSet = first_set,
      .descriptorSetCount = 1u, //
      .pDescriptorSets = &descriptor_set,
    };
    vkCmdBindDescriptorSets2KHR(handle_, &bind_desc_sets_info);
  }
  else
  {
    // LOG_CHECK(nullptr != currently_bound_pipeline_);

    // we  derive bindpoint from stage_flags..
    VkPipelineBindPoint bind_point{};
    if (stage_flags < VK_SHADER_STAGE_COMPUTE_BIT) {
      bind_point = (VkPipelineBindPoint)(
        bind_point | VK_PIPELINE_BIND_POINT_GRAPHICS
      );
    }
    if (stage_flags == VK_SHADER_STAGE_COMPUTE_BIT) {
      bind_point = (VkPipelineBindPoint)(
        bind_point | VK_PIPELINE_BIND_POINT_COMPUTE
      );
    }
    if (stage_flags > VK_SHADER_STAGE_COMPUTE_BIT) {
      bind_point = (VkPipelineBindPoint)(
        bind_point | VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
      );
    }

    vkCmdBindDescriptorSets(
      handle_,
      bind_point,
      pipeline_layout,
      first_set,
      1,
      &descriptor_set,
      0,
      nullptr
    );
  }
}

// ----------------------------------------------------------------------------

void GenericCommandEncoder::pushDescriptorSet(
  backend::PipelineInterface const& pipeline,
  uint32_t set,
  std::vector<DescriptorSetWriteEntry> const& entries
) const {
  DescriptorSetWriteEntry::Result out{};
  vk_utils::TransformDescriptorSetWriteEntries(
    VK_NULL_HANDLE,
    entries,
    out
  );

  // (requires VK_KHR_get_physical_device_properties2 or VK_VERSION_1_4)
  LOG_CHECK(vkCmdPushDescriptorSetKHR != nullptr);
  vkCmdPushDescriptorSetKHR(
    handle_,
    pipeline.bind_point(),
    pipeline.layout(),
    set,
    static_cast<uint32_t>(out.write_descriptor_sets.size()),
    out.write_descriptor_sets.data()
  );
}

// ----------------------------------------------------------------------------

void GenericCommandEncoder::pipelineBufferBarriers(
  std::vector<VkBufferMemoryBarrier2> barriers
) const {
  for (auto& bb : barriers) {
    bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    bb.srcQueueFamilyIndex = (bb.srcQueueFamilyIndex == 0u) ? VK_QUEUE_FAMILY_IGNORED
                                                            : bb.srcQueueFamilyIndex
                                                            ;
    bb.dstQueueFamilyIndex = (bb.dstQueueFamilyIndex == 0u) ? VK_QUEUE_FAMILY_IGNORED
                                                            : bb.dstQueueFamilyIndex
                                                            ;
    bb.size = (bb.size == 0ULL) ? VK_WHOLE_SIZE : bb.size;
  }
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .bufferMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
    .pBufferMemoryBarriers = barriers.data(),
  };
  // (requires VK_KHR_synchronization2 or VK_VERSION_1_3)
  vkCmdPipelineBarrier2(handle_, &dependency);
}

// ----------------------------------------------------------------------------

void GenericCommandEncoder::pipelineImageBarriers(
  std::vector<VkImageMemoryBarrier2> barriers
) const {
  // NOTE: we only have to set the old & new layout, as the mask will be automatically derived for generic cases
  //       when none are provided.

  for (auto& bb : barriers) {
    auto const [src_stage, src_access] = vk_utils::MakePipelineStageAccessTuple(bb.oldLayout);
    auto const [dst_stage, dst_access] = vk_utils::MakePipelineStageAccessTuple(bb.newLayout);
    bb.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    bb.srcStageMask        = (bb.srcStageMask == 0u) ? src_stage : bb.srcStageMask;
    bb.srcAccessMask       = (bb.srcAccessMask == 0u) ? src_access : bb.srcAccessMask;
    bb.dstStageMask        = (bb.dstStageMask == 0u) ? dst_stage : bb.dstStageMask;
    bb.dstAccessMask       = (bb.dstAccessMask == 0u) ? dst_access : bb.dstAccessMask;
    bb.srcQueueFamilyIndex = (bb.srcQueueFamilyIndex == 0u) ? VK_QUEUE_FAMILY_IGNORED : bb.srcQueueFamilyIndex; //
    bb.dstQueueFamilyIndex = (bb.dstQueueFamilyIndex == 0u) ? VK_QUEUE_FAMILY_IGNORED : bb.dstQueueFamilyIndex; //
    bb.subresourceRange    = (bb.subresourceRange.aspectMask == 0u) ? VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u}
                                                                    : bb.subresourceRange
                                                                    ;
  }
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size()),
    .pImageMemoryBarriers = barriers.data(),
  };
  vkCmdPipelineBarrier2(handle_, &dependency);
}

/* -------------------------------------------------------------------------- */

void CommandEncoder::copyBuffer(
  backend::Buffer const& src,
  backend::Buffer const& dst,
  std::vector<VkBufferCopy> const& regions
) const {
  vkCmdCopyBuffer(
    handle_, src.buffer, dst.buffer, static_cast<uint32_t>(regions.size()), regions.data()
  );
}

// ----------------------------------------------------------------------------

size_t CommandEncoder::copyBuffer(
  backend::Buffer const& src,
  size_t src_offset,
  backend::Buffer const& dst,
  size_t dst_offet,
  size_t size
) const {
  LOG_CHECK(size > 0);
  copyBuffer(src, dst, {
    {
      .srcOffset = src_offset,
      .dstOffset = dst_offet,
      .size = size,
    }
  });
  return src_offset + size;
}

// ----------------------------------------------------------------------------

void CommandEncoder::transitionImages(
  std::vector<backend::Image> const& images,
  VkImageLayout const src_layout,
  VkImageLayout const dst_layout,
  uint32_t layer_count
) const {
  /// [devnote] This is an helper method to transition multiple 2d single layer,
  //      single level images, using the default VkImageMemoryBarrier2 params
  //      as defined in 'GenericCommandEncoder::pipelineImageBarriers'.

  VkImageMemoryBarrier2 const barrier2{
    .oldLayout = src_layout,
    .newLayout = dst_layout,
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0u,
      .levelCount = 1u,
      .baseArrayLayer = 0u,
      .layerCount = layer_count
    },
  };
  std::vector<VkImageMemoryBarrier2> barriers(images.size(), barrier2);
  for (size_t i = 0u; i < images.size(); ++i) {
    barriers[i].image = images[i].image;
  }
  pipelineImageBarriers(barriers);
}

// ----------------------------------------------------------------------------

void CommandEncoder::blitImage2D(
  backend::Image const& src,
  VkImageLayout src_layout,
  backend::Image const& dst,
  VkImageLayout dst_layout,
  VkExtent2D const& extent,
  uint32_t layer_count
) const {
  auto const subresourceLayers = VkImageSubresourceLayers{
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .mipLevel = 0u,
    .baseArrayLayer = 0u,
    .layerCount = layer_count,
  };

  VkOffset3D const offsets[2]{
    {0, 0, 0},
    {
      .x = static_cast<int32_t>(extent.width),
      .y = static_cast<int32_t>(extent.height),
      .z = 1
    }
  };
  VkImageBlit const blit_region{
    .srcSubresource = subresourceLayers,
    .srcOffsets = { offsets[0], offsets[1] },
    .dstSubresource = subresourceLayers,
    .dstOffsets = { offsets[0], offsets[1] },
  };

  // transition_dst_layout must be either:
  //    * VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
  //    * VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL or
  //    * VK_IMAGE_LAYOUT_GENERAL
  auto const transition_src_layout{ VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL };
  auto const transition_dst_layout{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };

  auto const subresourceRange = VkImageSubresourceRange{
    .aspectMask = subresourceLayers.aspectMask,
    .baseMipLevel = 0u,
    .levelCount = 1u,
    .baseArrayLayer = subresourceLayers.baseArrayLayer,
    .layerCount = subresourceLayers.layerCount,
  };

  pipelineImageBarriers({
    {
      .oldLayout = src_layout,
      .newLayout = transition_src_layout,
      .image = src.image,
      .subresourceRange = subresourceRange,
    },
    {
      .oldLayout = dst_layout,
      .newLayout = transition_dst_layout,
      .image = dst.image,
      .subresourceRange = subresourceRange,
    },
  });

  vkCmdBlitImage(
    handle_,
    src.image, transition_src_layout,
    dst.image, transition_dst_layout,
    1u, &blit_region,
    VK_FILTER_LINEAR
  );

  pipelineImageBarriers({
    {
      .oldLayout = transition_src_layout,
      .newLayout = src_layout,
      .image = src.image,
      .subresourceRange = subresourceRange,
    },
    {
      .oldLayout = transition_dst_layout,
      .newLayout = dst_layout,
      .image = dst.image,
      .subresourceRange = subresourceRange,
    },
  });
}

// ----------------------------------------------------------------------------

void CommandEncoder::transferHostToDevice(
  void const* host_data,
  size_t const host_data_size,
  backend::Buffer const& device_buffer,
  size_t const device_buffer_offset
) const {
  LOG_CHECK(host_data != nullptr);
  LOG_CHECK(host_data_size > 0u);

  if (host_data_size < 65536u) {
    vkCmdUpdateBuffer(
      handle_,
      device_buffer.buffer,
      device_buffer_offset,
      host_data_size,
      host_data
    );
  } else {
    // [TODO] Staging buffers need better cleaning / garbage collection !
    auto staging_buffer{
      allocator_ptr_->createStagingBuffer(host_data_size, host_data)   //
    };
    copyBuffer(
      staging_buffer, 0u, device_buffer, device_buffer_offset, host_data_size
    );
  }
}

// ----------------------------------------------------------------------------

backend::Buffer CommandEncoder::createBufferAndUpload(
  void const* host_data,
  size_t const host_data_size,
  VkBufferUsageFlags2KHR const usage,
  size_t const device_buffer_offset,
  size_t const device_buffer_size
) const {
  LOG_CHECK(host_data != nullptr);
  LOG_CHECK(host_data_size > 0u);

  size_t const buffer_bytesize = (device_buffer_size > 0) ? device_buffer_size
                                                          : host_data_size
                                                          ;
  LOG_CHECK(host_data_size <= buffer_bytesize);

  auto device_buffer{allocator_ptr_->createBuffer(
    static_cast<VkDeviceSize>(buffer_bytesize),
    usage | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR,
    VMA_MEMORY_USAGE_GPU_ONLY
  )};
  transferHostToDevice(
    host_data, host_data_size, device_buffer, device_buffer_offset
  );

  return device_buffer;
}

// ----------------------------------------------------------------------------

RenderPassEncoder CommandEncoder::beginRendering(RenderPassDescriptor const& desc) const {
  auto const rendering_info = VkRenderingInfoKHR{
    .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
    .pNext                = nullptr,
    .flags                = 0b0u,
    .renderArea           = desc.renderArea,
    .layerCount           = 1u,
    .viewMask             = desc.viewMask,
    .colorAttachmentCount = static_cast<uint32_t>(desc.colorAttachments.size()),
    .pColorAttachments    = desc.colorAttachments.data(),
    .pDepthAttachment     = &desc.depthAttachment,
    .pStencilAttachment   = &desc.stencilAttachment, //
  };
  vkCmdBeginRenderingKHR(handle_, &rendering_info);

  return RenderPassEncoder(handle_, target_queue_index());
}

// ----------------------------------------------------------------------------

RenderPassEncoder CommandEncoder::beginRendering(
  backend::RTInterface const& render_target
) const {
  auto const& colors = render_target.color_attachments();
  auto depthStencilImageView = render_target.depth_stencil_attachment().view;

  /* Dynamic rendering required color images to be in the COLOR_ATTACHMENT layout. */
  transitionImages(
    colors,
    VK_IMAGE_LAYOUT_UNDEFINED,
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    render_target.layer_count()
  );

  auto desc = RenderPassDescriptor{
    .colorAttachments = {},
    .depthAttachment = {
      .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
      .imageView   = depthStencilImageView,
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
      .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue  = render_target.depth_stencil_clear_value(),
    },
    .stencilAttachment = {
      .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
      .imageView   = depthStencilImageView,
      .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
      .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue  = render_target.depth_stencil_clear_value(),
    },
    .renderArea = {
      .offset = {0, 0},
      .extent = render_target.surface_size()
    },
    .viewMask = render_target.view_mask(),
  };

  // Setup the COLOR attachment depending on MSAA usage.
  desc.colorAttachments.resize(colors.size(), VkRenderingAttachmentInfo{
    .sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
    .imageLayout        = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR,
    .resolveMode        = VK_RESOLVE_MODE_NONE,
    .resolveImageView   = VK_NULL_HANDLE,
    .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    .storeOp            = VK_ATTACHMENT_STORE_OP_STORE,
  });
  if (render_target.use_msaa()) {
    for (size_t i = 0u; i < colors.size(); ++i) {
      auto& attach = desc.colorAttachments[i];
      attach.imageView          = colors[i].view; //
      attach.loadOp             = render_target.color_load_op(i);
      attach.storeOp            = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attach.clearValue         = render_target.color_clear_value(i);
      attach.resolveMode        = VK_RESOLVE_MODE_AVERAGE_BIT; //
      attach.resolveImageView   = render_target.resolve_attachment(i).view;
      attach.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
  } else {
    for (size_t i = 0u; i < colors.size(); ++i) {
      auto& attach = desc.colorAttachments[i];
      attach.imageView  = colors[i].view;
      attach.loadOp     = render_target.color_load_op(i);
      attach.clearValue = render_target.color_clear_value(i);
    }
  }

  current_render_target_ptr_ = &render_target;

  return beginRendering(desc);
}

// ----------------------------------------------------------------------------

RenderPassEncoder CommandEncoder::beginRendering() const {
  LOG_CHECK( default_render_target_ptr_ != nullptr );
  auto pass = beginRendering( *default_render_target_ptr_ );
  pass.setViewportScissor(default_render_target_ptr_->surface_size()); //
  return pass;
}

// ----------------------------------------------------------------------------

void CommandEncoder::endRendering() const {
  vkCmdEndRendering(handle_);

  // Transition the color buffers to "shader read only".
  if (current_render_target_ptr_ != nullptr) [[likely]]
  {
    transitionImages(
      current_render_target_ptr_->resolve_attachments(),
      VK_IMAGE_LAYOUT_UNDEFINED,
      // -----------------------------
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      // VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      // -----------------------------
      current_render_target_ptr_->layer_count()
    );
    current_render_target_ptr_ = nullptr;
  }
}

// ----------------------------------------------------------------------------

RenderPassEncoder CommandEncoder::beginRenderPass(backend::RPInterface const& render_pass) const {
  auto const& clear_values = render_pass.clear_values();

  VkRenderPassBeginInfo const render_pass_begin_info{
    .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .renderPass = render_pass.render_pass(),
    .framebuffer = render_pass.swap_attachment(),
    .renderArea = {
      .extent = render_pass.surface_size(),
    },
    .clearValueCount = static_cast<uint32_t>(clear_values.size()),
    .pClearValues = clear_values.data(),
  };
  vkCmdBeginRenderPass(handle_, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

  return RenderPassEncoder(handle_, target_queue_index());
}

// ----------------------------------------------------------------------------

void CommandEncoder::endRenderPass() const {
  vkCmdEndRenderPass(handle_);
}

/* -------------------------------------------------------------------------- */

void RenderPassEncoder::setViewport(float x, float y, float width, float height, bool flip_y) const {
  VkViewport const vp{
    .x = x,
    .y = y + (flip_y ? height : 0.0f),
    .width = width,
    .height = height * (flip_y ? -1.0f : 1.0f),
    .minDepth = 0.0f,
    .maxDepth = 1.0f,
  };
  vkCmdSetViewport(handle_, 0u, 1u, &vp);
}

// ----------------------------------------------------------------------------

void RenderPassEncoder::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) const {
  VkRect2D const rect{
    .offset = {
      .x = x,
      .y = y,
    },
    .extent = {
      .width = width,
      .height = height,
    },
  };
  vkCmdSetScissor(handle_, 0u, 1u, &rect);
}

// ----------------------------------------------------------------------------

void RenderPassEncoder::setViewportScissor(VkRect2D const rect, bool flip_y) const {
  float const x = static_cast<float>(rect.offset.x);
  float const y = static_cast<float>(rect.offset.y);
  float const w = static_cast<float>(rect.extent.width);
  float const h = static_cast<float>(rect.extent.height);
  setViewport(x, y, w, h, flip_y);
  setScissor(rect.offset.x, rect.offset.y, rect.extent.width, rect.extent.height);
}

// ----------------------------------------------------------------------------

void RenderPassEncoder::draw(
  DrawDescriptor const& desc,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer
) const {
  // Vertex Input.
  {
    auto const& vi{desc.vertexInput};

    // (shoud be disabled when vertex input is not dynamic)
    setVertexInput(vi);

    for (size_t i = 0; i < vi.bindings.size(); ++i) {
      bindVertexBuffer(vertex_buffer, vi.bindings[i].binding, vi.vertexBufferOffsets[i]);
    }
  }

  // Topology.
  // setPrimitiveTopology(desc.topology);

  // Draw.
  if (desc.indexCount > 0u) [[likely]] {
    bindIndexBuffer(index_buffer, desc.indexType, desc.indexOffset);
    drawIndexed(desc.indexCount, desc.instanceCount);
  } else {
    draw(desc.vertexCount, desc.instanceCount);
  }
}

/* -------------------------------------------------------------------------- */
