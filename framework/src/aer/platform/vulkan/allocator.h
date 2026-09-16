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

  // Dynamic Uniforms / Staging Buffers / Initialized once.
  static constexpr VmaAllocationCreateFlags kAllocMappedAtCreation{
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
  };

  // Readback buffers / GPU queries.
  static constexpr VmaAllocationCreateFlags kAllocReadback{
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
  };

  // Dedicated memory for *large* buffer.
  static constexpr VmaAllocationCreateFlags kAllocDedicated{
    VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
  };

 public:
  Allocator() = default;
  ~Allocator() = default;

  void init(VmaAllocatorCreateInfo alloc_create_info);

  void release();

  // ----- Buffer -----

  [[nodiscard]]
  backend::Buffer createBuffer(
    std::string const &name,
    VkDeviceSize const size,
    VkBufferUsageFlags2KHR const usage,
    VmaMemoryUsage const memory_usage = VMA_MEMORY_USAGE_AUTO,
    VmaAllocationCreateFlags const flags = {}
  ) const;

  [[nodiscard]]
  backend::Buffer createBuffer(
    VkDeviceSize const size,
    VkBufferUsageFlags2KHR const usage,
    VmaMemoryUsage const memory_usage = VMA_MEMORY_USAGE_AUTO,
    VmaAllocationCreateFlags const flags = {}
  ) const {
    return createBuffer("", size, usage, memory_usage, flags);
  }

  void destroyBuffer(backend::Buffer const& buffer) const {
    if (buffer.buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(handle_, buffer.buffer, buffer.allocation);
    }
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

  void flushBuffer(backend::Buffer const& buffer, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE) const {
    vmaFlushAllocation(handle_, buffer.allocation, offset, size);
    buffer.mapped_data = nullptr; //
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
