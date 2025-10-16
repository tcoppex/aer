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

  void release();

  // ----- Buffer -----

  [[nodiscard]]
  backend::Buffer createBuffer(
    VkDeviceSize const size,
    VkBufferUsageFlags2KHR const usage,   // !! require maintenance5 !!
    VmaMemoryUsage const memory_usage = VMA_MEMORY_USAGE_AUTO,
    VmaAllocationCreateFlags const flags = {}
  ) const;

  void destroyBuffer(backend::Buffer const& buffer) const {
    vmaDestroyBuffer(handle_, buffer.buffer, buffer.allocation);
  }

  [[nodiscard]]
  backend::Buffer createStagingBuffer(
    size_t const bytesize = kDefaultStagingBufferSize,
    void const* host_data = nullptr,
    size_t host_data_size = 0u
  ) const;

  void clearStagingBuffers() const;

  void mapMemory(backend::Buffer const& buffer, void **data) const {
    CHECK_VK( vmaMapMemory(handle_, buffer.allocation, data) );
  }

  void unmapMemory(backend::Buffer const& buffer) const {
    vmaUnmapMemory(handle_, buffer.allocation);
  }

  /* Alias to map & copy host data to a device buffer. */
  size_t writeBuffer(
    backend::Buffer const& dst_buffer,
    size_t const dst_offset,
    void const* host_data,
    size_t const host_offset,
    size_t const bytesize
  ) const;

  size_t writeBuffer(
    backend::Buffer const& dst_buffer,
    void const* host_data,
    size_t const bytesize
  ) const {
    return writeBuffer(dst_buffer, 0u, host_data, 0u, bytesize);
  }

  // ----- Image -----

  /* Create an image with view with identical format. */
  backend::Image createImage(
    VkImageCreateInfo const& image_info,
    VkImageViewCreateInfo view_info,
    VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY
  ) const;

  void destroyImage(backend::Image &image) const;

 private:
  VkDevice device_{};
  VmaAllocator handle_{};
  mutable std::vector<backend::Buffer> staging_buffers_{};
};

} // namespace "backend"

/* -------------------------------------------------------------------------- */

#endif // AER_PLATFORM_VULKAN_ALLOCATOR_H_
