#ifndef AER_PLATFORM_BACKEND_ALLOCATOR_H_
#define AER_PLATFORM_BACKEND_ALLOCATOR_H_

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/backend/types.h"
#include "aer/platform/backend/vk_utils.h"

/* -------------------------------------------------------------------------- */

class ResourceAllocator {
 public:
  static constexpr size_t kDefaultStagingBufferSize{ 32u * 1024u * 1024u };
  static constexpr bool kAutoAlignBufferSize{ false };

 public:
  ResourceAllocator() = default;
  ~ResourceAllocator() = default;

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

  [[nodiscard]]
  backend::Buffer create_staging_buffer(
    size_t const bytesize = kDefaultStagingBufferSize,
    void const* host_data = nullptr,
    size_t host_data_size = 0u
  ) const;

  template<typename T> [[nodiscard]]
  backend::Buffer create_staging_buffer(std::span<T> const& host_data) const {
    return create_staging_buffer(sizeof(T) * host_data.size(), host_data.data());
  }

  void map_memory(backend::Buffer const& buffer, void **data) const {
    CHECK_VK( vmaMapMemory(allocator_, buffer.allocation, data) );
  }

  void unmap_memory(backend::Buffer const& buffer) const {
    vmaUnmapMemory(allocator_, buffer.allocation);
  }

  // (should the allocator be allowed to write on device ?)
  // ------------------------
  size_t write_buffer(
    backend::Buffer const& dst_buffer,
    size_t const dst_offset,
    void const* host_data,
    size_t const host_offset,
    size_t const bytesize
  ) const;

  void upload_host_to_device(
    void const* host_data,
    size_t const bytesize,
    backend::Buffer const& dst_buffer
  ) const {
    write_buffer(dst_buffer, 0u, host_data, 0u, bytesize);
  }
  // ------------------------

  void destroy_buffer(backend::Buffer const& buffer) const {
    vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
  }

  void clear_staging_buffers() const;

  // ----- Image -----

  void create_image(VkImageCreateInfo const& image_info, backend::Image *image) const;

  /* Create an image with view with identical format. */
  void create_image_with_view(
    VkImageCreateInfo const& image_info,
    VkImageViewCreateInfo const& view_info,
    backend::Image *image
  ) const;

  void destroy_image(backend::Image *image) const;

 private:
  VkDevice device_{};
  VmaAllocator allocator_{};
  mutable std::vector<backend::Buffer> staging_buffers_{};
};

/* -------------------------------------------------------------------------- */

#endif
