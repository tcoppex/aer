
#define VMA_IMPLEMENTATION
#define VMA_LEAK_LOG_FORMAT(format, ...)        \
  {                                             \
    fprintf(stderr, (format), __VA_ARGS__);     \
    fprintf(stderr, "\n");                      \
  }
#include "framework/backend/allocator.h"

/* -------------------------------------------------------------------------- */

void ResourceAllocator::init(VmaAllocatorCreateInfo alloc_create_info) {
  device_ = alloc_create_info.device;

  VmaVulkanFunctions const functions{
    .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
    .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
  };

  alloc_create_info.pVulkanFunctions = &functions;
  alloc_create_info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
                          | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT
                          | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT
                   ;
  vmaCreateAllocator(&alloc_create_info, &allocator_);
}

// ----------------------------------------------------------------------------

void ResourceAllocator::deinit() {
  clear_staging_buffers();
  vmaDestroyAllocator(allocator_);
}

// ----------------------------------------------------------------------------

Buffer_t ResourceAllocator::create_buffer(
  VkDeviceSize size,
  VkBufferUsageFlags2KHR usage,
  VmaMemoryUsage memory_usage,
  VmaAllocationCreateFlags flags
) const {
  Buffer_t buffer{};

  if constexpr (kAutoAlignBufferSize) {
    if (auto const new_size{ utils::AlignTo256(size) }; new_size != size) {
      LOGW("%s: change size from %lu to %lu.\n", __FUNCTION__, (uint64_t)size, (uint64_t)new_size);
      size = new_size;
    }
  }

  VkBufferUsageFlags2CreateInfoKHR const usage_flag2_info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO_KHR,
    .usage = usage
           | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR
           ,
  };

  // Create buffer.
  VkBufferCreateInfo const buffer_info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext = &usage_flag2_info,
    .size = size,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };
  VmaAllocationCreateInfo alloc_create_info{
    .flags = flags,
    .usage = memory_usage,
  };
  VmaAllocationInfo result_alloc_info{};
  CHECK_VK(
    vmaCreateBuffer(allocator_, &buffer_info, &alloc_create_info, &buffer.buffer, &buffer.allocation, &result_alloc_info)
  );

  // Get its GPU address.
  VkBufferDeviceAddressInfo const buffer_device_addr_info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
    .buffer = buffer.buffer,
  };
  buffer.address = vkGetBufferDeviceAddressKHR(device_, &buffer_device_addr_info);

  return buffer;
}

// ----------------------------------------------------------------------------

Buffer_t ResourceAllocator::create_staging_buffer(size_t const bytesize, void const* host_data, size_t host_data_size) {
  assert(host_data_size <= bytesize);

  // TODO : use a pool to reuse some staging buffer.

  // Create buffer.
  Buffer_t staging_buffer{create_buffer(
    static_cast<VkDeviceSize>(bytesize),
    VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
    VMA_MEMORY_USAGE_CPU_TO_GPU,
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
  )};
  // Map host data to device.
  if (host_data != nullptr) {
    upload_host_to_device(host_data, (host_data_size > 0u) ? host_data_size : bytesize, staging_buffer);
  }
  staging_buffers_.push_back(staging_buffer);
  return staging_buffer;
}

// ----------------------------------------------------------------------------

void ResourceAllocator::write_buffer(
  Buffer_t const& dst_buffer, size_t const dst_offset,
  void const* host_data, size_t const host_offset, size_t const bytesize
) {
  assert(host_data != nullptr);
  assert(dst_buffer.buffer != VK_NULL_HANDLE);
  assert(bytesize > 0);

  void *device_data = nullptr;
  CHECK_VK( vmaMapMemory(allocator_, dst_buffer.allocation, &device_data) );

  memcpy(static_cast<char*>(device_data) + dst_offset,
         static_cast<const char*>(host_data) + host_offset, bytesize);

  vmaUnmapMemory(allocator_, dst_buffer.allocation);
}

// ----------------------------------------------------------------------------

void ResourceAllocator::clear_staging_buffers() {
  for (auto const& staging_buffer : staging_buffers_) {
    destroy_buffer(staging_buffer);
  }
  staging_buffers_.clear();
}

// ----------------------------------------------------------------------------

void ResourceAllocator::create_image(VkImageCreateInfo const& image_info, Image_t *image) const {
  assert( image_info.format != VK_FORMAT_UNDEFINED );

  VmaAllocationCreateInfo const alloc_create_info{
    .usage = VMA_MEMORY_USAGE_GPU_ONLY,
  };
  VmaAllocationInfo alloc_info{};
  CHECK_VK(vmaCreateImage(
    allocator_, &image_info, &alloc_create_info, &image->image, &image->allocation, &alloc_info
  ));
  image->format = image_info.format;
}

// ----------------------------------------------------------------------------

void ResourceAllocator::create_image_with_view(VkImageCreateInfo const& image_info, VkImageViewCreateInfo const& view_info, Image_t *image) const {
  assert( view_info.format == image_info.format );

  create_image(image_info, image);
  auto info{view_info};
  info.image = image->image;
  CHECK_VK(vkCreateImageView(device_, &info, nullptr, &image->view));
}

// ----------------------------------------------------------------------------

void ResourceAllocator::destroy_image(Image_t *image) const {
  vmaDestroyImage(allocator_, image->image, image->allocation);
  image->image = VK_NULL_HANDLE;
  if (image->view != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, image->view, nullptr);
    image->image = VK_NULL_HANDLE;
  }
}

/* -------------------------------------------------------------------------- */
