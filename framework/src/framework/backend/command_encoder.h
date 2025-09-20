#ifndef VKFRAMEWORK_BACKEND_COMMAND_ENCODER_H
#define VKFRAMEWORK_BACKEND_COMMAND_ENCODER_H

#include "framework/backend/allocator.h"
#include "framework/backend/types.h"
#include "framework/backend/vk_utils.h"

class RenderPassEncoder;
class PostFxInterface;

/* -------------------------------------------------------------------------- */

/**
 * Interface to VkCommandBuffer wrappers.
 *
 * Specify commands shared by all wrappers.
 */
class GenericCommandEncoder {
 public:
  GenericCommandEncoder() = default;

  GenericCommandEncoder(VkCommandBuffer command_buffer, uint32_t target_queue_index)
    : command_buffer_(command_buffer)
    , target_queue_index_{target_queue_index}
    , currently_bound_pipeline_layout_{VK_NULL_HANDLE}
  {}

  virtual ~GenericCommandEncoder() {}

  VkCommandBuffer get_handle() {
    return command_buffer_;
  }

  uint32_t get_target_queue_index() const {
    return target_queue_index_;
  }

  // --- Descriptor Sets ---

  void bind_descriptor_set(
    VkDescriptorSet const descriptor_set,
    VkPipelineLayout const pipeline_layout,
    VkShaderStageFlags const stage_flags,
    uint32_t first_set = 0u
  ) const;

  void bind_descriptor_set(
    VkDescriptorSet const descriptor_set,
    VkShaderStageFlags const stage_flags
  ) const {
    LOG_CHECK(VK_NULL_HANDLE != currently_bound_pipeline_layout_);
    bind_descriptor_set(descriptor_set, currently_bound_pipeline_layout_, stage_flags);
  }

  void push_descriptor_set(
    backend::PipelineInterface const& pipeline,
    uint32_t set,
    std::vector<DescriptorSetWriteEntry> const& entries
  ) const;

  // --- Push Constants ---

  template<typename T> requires (!SpanConvertible<T>)
  void push_constant(
    T const& value,
    VkPipelineLayout const pipeline_layout,
    VkShaderStageFlags const stage_flags = VK_SHADER_STAGE_ALL_GRAPHICS,
    uint32_t const offset = 0u
  ) const {
    VkPushConstantsInfoKHR const push_info{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = pipeline_layout,
      .stageFlags = stage_flags,
      .offset = offset,
      .size = static_cast<uint32_t>(sizeof(T)),
      .pValues = &value,
    };
    vkCmdPushConstants2KHR(command_buffer_, &push_info);
  }

  template<typename T> requires (SpanConvertible<T>)
  void push_constants(
    T const& values,
    VkPipelineLayout const pipeline_layout,
    VkShaderStageFlags const stage_flags = VK_SHADER_STAGE_ALL_GRAPHICS,
    uint32_t const offset = 0u
  ) const {
    auto const span_values{ std::span(values) };
    VkPushConstantsInfoKHR const push_info{
      .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR,
      .layout = pipeline_layout,
      .stageFlags = stage_flags,
      .offset = offset,
      .size = sizeof(typename decltype(span_values)::value_type) * span_values.size(),
      .pValues = span_values.data(),
    };
    vkCmdPushConstants2KHR(command_buffer_, &push_info);
  }

  template<typename T> requires (!SpanConvertible<T>)
  void push_constant(T const& value, VkShaderStageFlags const stage_flags = VK_SHADER_STAGE_ALL_GRAPHICS, uint32_t const offset = 0u) const {
    LOG_CHECK(VK_NULL_HANDLE != currently_bound_pipeline_layout_);
    push_constant(value, currently_bound_pipeline_layout_, stage_flags, offset);
  }

  template<typename T> requires (SpanConvertible<T>)
  void push_constants(T const& values, VkShaderStageFlags const stage_flags = VK_SHADER_STAGE_ALL_GRAPHICS, uint32_t const offset = 0u) const {
    LOG_CHECK(VK_NULL_HANDLE != currently_bound_pipeline_layout_);
    push_constants(values, currently_bound_pipeline_layout_, stage_flags, offset);
  }

  // --- Pipeline ---

  void bind_pipeline(backend::PipelineInterface const& pipeline) {
    currently_bound_pipeline_layout_ = pipeline.get_layout();
    vkCmdBindPipeline(command_buffer_, pipeline.get_bind_point(), pipeline.get_handle());
  }

  void bind_pipeline(backend::PipelineInterface const& pipeline) const {
    vkCmdBindPipeline(command_buffer_, pipeline.get_bind_point(), pipeline.get_handle());
  }

  // --- Pipeline Barrier ---

  void pipeline_buffer_barriers(std::vector<VkBufferMemoryBarrier2> barriers) const;

  void pipeline_image_barriers(std::vector<VkImageMemoryBarrier2> barriers) const;

  // --- Compute ---

  template<uint32_t tX = 1u, uint32_t tY = 1u, uint32_t tZ = 1u>
  void dispatch(uint32_t x = 1u, uint32_t y = 1u, uint32_t z = 1u) const {
    LOG_CHECK(x > 0u);
    LOG_CHECK(y > 0u);
    LOG_CHECK(z > 0u);

    vkCmdDispatch(command_buffer_,
      vkutils::GetKernelGridDim(x, tX),
      vkutils::GetKernelGridDim(y, tY),
      vkutils::GetKernelGridDim(z, tZ)
    );
  }

  // --- Ray Tracing ---

  void trace_rays(backend::RayTracingAddressRegion const& region, uint32_t width, uint32_t height, uint32_t depth = 1u) {
    vkCmdTraceRaysKHR(
      command_buffer_,
      &region.raygen,
      &region.miss,
      &region.hit,
      &region.callable,
      width,
      height,
      depth
    );
  }

 protected:
  VkCommandBuffer command_buffer_{};
  uint32_t target_queue_index_{};

 private:
  VkPipelineLayout currently_bound_pipeline_layout_{};
};

/* -------------------------------------------------------------------------- */

/**
 * Main wrapper used for general operations outside rendering.
 **/
class CommandEncoder : public GenericCommandEncoder {
 public:
  ~CommandEncoder() {}

  // --- Buffers ---

  void copy_buffer(backend::Buffer const& src, backend::Buffer const& dst, std::vector<VkBufferCopy> const& regions) const;

  size_t copy_buffer(backend::Buffer const& src, size_t src_offset, backend::Buffer const& dst, size_t dst_offet, size_t size) const;

  size_t copy_buffer(backend::Buffer const& src, backend::Buffer const& dst, size_t size) const {
    return copy_buffer(src, 0, dst, 0, size);
  }

  void transfer_host_to_device(void const* host_data, size_t const host_data_size, backend::Buffer const& device_buffer, size_t const device_buffer_offset = 0u) const;

  backend::Buffer create_buffer_and_upload(void const* host_data, size_t const host_data_size, VkBufferUsageFlags2KHR const usage, size_t const device_buffer_offset = 0u, size_t const device_buffer_size = 0u) const;

  template<typename T> requires (SpanConvertible<T>)
  backend::Buffer create_buffer_and_upload(T const& host_data, VkBufferUsageFlags2KHR const usage = {}, size_t const device_buffer_offset = 0u, size_t const device_buffer_size = 0u) const {
    auto const host_span{ std::span(host_data) };
    size_t const bytesize{ sizeof(typename decltype(host_span)::element_type) * host_span.size() };
    return create_buffer_and_upload(host_span.data(), bytesize, usage, device_buffer_offset, device_buffer_size);
  }

  // --- Images ---

  void transition_images_layout(std::vector<backend::Image> const& images, VkImageLayout const src_layout, VkImageLayout const dst_layout) const;

  void copy_buffer_to_image(backend::Buffer const& src, backend::Image const& dst, VkExtent3D extent, VkImageLayout image_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) const {
    VkBufferImageCopy const copy{
      .bufferOffset = 0lu,
      .bufferRowLength = 0u,
      .bufferImageHeight = 0u,
      .imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .mipLevel = 0u,
        .baseArrayLayer = 0u,
        .layerCount = 1u,
      },
      .imageOffset = {},
      .imageExtent = extent,
    };
    vkCmdCopyBufferToImage(command_buffer_, src.buffer, dst.image, image_layout, 1u, &copy);
  }

  void blit_image_2d(backend::Image const& src, VkImageLayout src_layout, backend::Image const& dst, VkImageLayout dst_layout, VkExtent2D const& extent) const;

  void blit(PostFxInterface const& fx_src, backend::RTInterface const& rt_dst) const;

  // --- Rendering ---

  /* Dynamic rendering. */
  RenderPassEncoder begin_rendering(RenderPassDescriptor const& desc) const;
  RenderPassEncoder begin_rendering(backend::RTInterface const& render_target);
  RenderPassEncoder begin_rendering(std::shared_ptr<backend::RTInterface> render_target);
  RenderPassEncoder begin_rendering();
  void end_rendering();

  /* Legacy rendering. */
  RenderPassEncoder begin_render_pass(backend::RPInterface const& render_pass) const;
  void end_render_pass() const;

  // --- UI ----

  void render_ui(backend::RTInterface &render_target);


 protected:
  CommandEncoder() = default;

 private:
  CommandEncoder(
    VkCommandBuffer const command_buffer,
    uint32_t const target_queue_index,
    VkDevice const device,
    ResourceAllocator *allocator_ptr
  ) : GenericCommandEncoder(command_buffer, target_queue_index)
    , device_{device}
    , allocator_ptr_{allocator_ptr}
  {}

  void begin() const {
    VkCommandBufferBeginInfo const cb_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    CHECK_VK( vkBeginCommandBuffer(command_buffer_, &cb_begin_info) );
  }

  void end() const {
    CHECK_VK( vkEndCommandBuffer(command_buffer_) );
  }

 protected:
  VkDevice device_{};
  // ResourceAllocator const* allocator_ptr_{};
  ResourceAllocator* allocator_ptr_{};

  /* Link the default backend::RTInterface when one is available. */
  backend::RTInterface const* default_render_target_ptr_{};

  /* Link the bound backend::RTInterface for auto layout transition. */
  backend::RTInterface const* current_render_target_ptr_{};

 public:
  friend class Context;
  friend class Renderer;
};

/* -------------------------------------------------------------------------- */

/**
 * Specialized wrapper for rendering operations.
 **/
class RenderPassEncoder : public GenericCommandEncoder {
 public:
  static constexpr bool kDefaultViewportFlipY{ true };

 public:
  ~RenderPassEncoder() {}

  // --- Dynamic States ---

  void set_viewport(float x, float y, float width, float height, bool flip_y = kDefaultViewportFlipY) const;

  void set_scissor(int32_t x, int32_t y, uint32_t width, uint32_t height) const;

  void set_viewport_scissor(VkRect2D const rect, bool flip_y = kDefaultViewportFlipY) const;

  void set_viewport_scissor(VkExtent2D const extent, bool flip_y = kDefaultViewportFlipY) const {
    set_viewport_scissor({{0, 0}, extent}, flip_y);
  }

  void set_primitive_topology(VkPrimitiveTopology const topology) const {
    // VK_EXT_extended_dynamic_state or VK_VERSION_1_3
    vkCmdSetPrimitiveTopologyEXT(command_buffer_, topology);
  }

  void set_vertex_input(VertexInputDescriptor const& vertex_input_descriptor) const {
    vkCmdSetVertexInputEXT(
      command_buffer_,
      static_cast<uint32_t>(vertex_input_descriptor.bindings.size()),
      vertex_input_descriptor.bindings.data(),
      static_cast<uint32_t>(vertex_input_descriptor.attributes.size()),
      vertex_input_descriptor.attributes.data()
    );
  }

  // --- Buffer binding ---

  void bind_vertex_buffer(backend::Buffer const& buffer, uint32_t binding = 0u, uint64_t const offset = 0u) const {
    vkCmdBindVertexBuffers(command_buffer_, binding, 1u, &buffer.buffer, &offset);
  }

  void bind_vertex_buffer(backend::Buffer const& buffer, uint32_t binding, uint64_t const offset, uint64_t const stride) const {
    // VK_EXT_extended_dynamic_state or VK_VERSION_1_3
    vkCmdBindVertexBuffers2(command_buffer_, binding, 1u, &buffer.buffer, &offset, nullptr, &stride);
  }

  void bind_index_buffer(backend::Buffer const& buffer, VkIndexType const index_type = VK_INDEX_TYPE_UINT32, VkDeviceSize const offset = 0u, VkDeviceSize const size = VK_WHOLE_SIZE) const {
    // VK_KHR_maintenance5 or VK_VERSION_1_4
    vkCmdBindIndexBuffer2KHR(command_buffer_, buffer.buffer, offset, size, index_type);
  }

  // --- Draw ---

  void draw(uint32_t vertex_count,
            uint32_t instance_count = 1u,
            uint32_t first_vertex = 0u,
            uint32_t first_instance = 0u) const {
    vkCmdDraw(command_buffer_, vertex_count, instance_count, first_vertex, first_instance);
  }

  void draw_indexed(uint32_t index_count,
                    uint32_t instance_count = 1u,
                    uint32_t first_index = 0u,
                    int32_t vertex_offset = 0,
                    uint32_t first_instance = 0u) const {
    vkCmdDrawIndexed(command_buffer_, index_count, instance_count, first_index, vertex_offset, first_instance);
  }

  // [WIP]
  void draw(DrawDescriptor const& desc, backend::Buffer const& vertex_buffer, backend::Buffer const& index_buffer) const;

 private:
  RenderPassEncoder(VkCommandBuffer const command_buffer, uint32_t target_queue_index)
    : GenericCommandEncoder(command_buffer, target_queue_index)
  {}

 public:
  friend class CommandEncoder;
};

/* -------------------------------------------------------------------------- */

#endif
