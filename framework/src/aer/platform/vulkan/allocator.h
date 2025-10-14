#ifndef AER_PLATFORM_VULKAN_ALLOCATOR_H_
#define AER_PLATFORM_VULKAN_ALLOCATOR_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/vulkan/types.h"
#include "aer/platform/vulkan/utils.h"

/* -------------------------------------------------------------------------- */

namespace backend {

class Allocator {
 public:
  static constexpr size_t kDefaultStagingBufferSize{ 32u * 1024u * 1024u };
  static constexpr bool kAutoAlignBufferSize{ false };

 public:
  Allocator() = default;
  ~Allocator() = default;

  void init(VmaAllocatorCreateInfo alloc_create_info);

  void deinit();

  // ----- Buffer -----

  [[nodiscard]]
  backend::Buffer create_buffer(
    VkDeviceSize const size,
    VkBufferUsageFlags2KHR const usage,   // !! require maintenance5 !!
    VmaMemoryUsage const memory_usage = VMA_MEMORY_USAGE_AUTO,
    VmaAllocationCreateFlags const flags = {}
  ) const;

  void destroy_buffer(backend::Buffer const& buffer) const {
    vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
  }

  [[nodiscard]]
  backend::Buffer create_staging_buffer(
    size_t const bytesize = kDefaultStagingBufferSize,
    void const* host_data = nullptr,
    size_t host_data_size = 0u
  ) const;

  void clear_staging_buffers() const;

  void map_memory(backend::Buffer const& buffer, void **data) const {
    CHECK_VK( vmaMapMemory(allocator_, buffer.allocation, data) );
  }

  void unmap_memory(backend::Buffer const& buffer) const {
    vmaUnmapMemory(allocator_, buffer.allocation);
  }

  /* Alias to map & copy host data to a device buffer. */
  size_t write_buffer(
    backend::Buffer const& dst_buffer,
    size_t const dst_offset,
    void const* host_data,
    size_t const host_offset,
    size_t const bytesize
  ) const;

  size_t write_buffer(
    backend::Buffer const& dst_buffer,
    void const* host_data,
    size_t const bytesize
  ) const {
    return write_buffer(dst_buffer, 0u, host_data, 0u, bytesize);
  }

  // ----- Image -----

  /* Create an image with view with identical format. */
  backend::Image create_image(
    VkImageCreateInfo const& image_info,
    VkImageViewCreateInfo view_info,
    VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY
  ) const;

  void destroy_image(backend::Image &image) const;

 private:
  VkDevice device_{};
  VmaAllocator allocator_{};
  mutable std::vector<backend::Buffer> staging_buffers_{};
};

} // namespace "backend"

/* -------------------------------------------------------------------------- */

#endif // AER_PLATFORM_VULKAN_ALLOCATOR_H_
