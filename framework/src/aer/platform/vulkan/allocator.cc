#define VMA_IMPLEMENTATION
#define VMA_LEAK_LOG_FORMAT(format, ...)        \
  {                                             \
    fprintf(stderr, (format), __VA_ARGS__);     \
    fprintf(stderr, "\n");                      \
  }

#include "aer/platform/vulkan/allocator.h"
#include "aer/platform/vulkan/utils.h"
#include "aer/core/utils.h"

namespace backend {

/* -------------------------------------------------------------------------- */

void Allocator::init(VmaAllocatorCreateInfo alloc_create_info) {
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
  vmaCreateAllocator(&alloc_create_info, &handle_);
}

// ----------------------------------------------------------------------------

void Allocator::release() {
  clear_staging_buffers();
  vmaDestroyAllocator(handle_);
}

// ----------------------------------------------------------------------------

backend::Buffer Allocator::create_buffer(
  VkDeviceSize size,
  VkBufferUsageFlags2KHR usage,
  VmaMemoryUsage memory_usage,
  VmaAllocationCreateFlags flags
) const {
  backend::Buffer buffer{};

  if constexpr (kAutoAlignBufferSize) {
    if (auto const new_size{ utils::AlignTo256(size) }; new_size != size) {
      LOGW("{}: change size from {} to {}.\n", __FUNCTION__, uint32_t(size), uint32_t(new_size));
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
  CHECK_VK(vmaCreateBuffer(
    handle_,
    &buffer_info,
    &alloc_create_info,
    &buffer.buffer,
    &buffer.allocation,
    &result_alloc_info
  ));

  // Get its GPU address.
  VkBufferDeviceAddressInfoKHR const buffer_device_addr_info{
    .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
    .buffer = buffer.buffer,
  };
  buffer.address = vkGetBufferDeviceAddressKHR(device_, &buffer_device_addr_info);

  return buffer;
}

// ----------------------------------------------------------------------------

backend::Buffer Allocator::create_staging_buffer(
  size_t const bytesize,
  void const* host_data,
  size_t host_data_size
) const {
  LOG_CHECK( host_data_size <= bytesize );

  // TODO : use a pool to reuse some staging buffer.

  // Create buffer.
  backend::Buffer staging_buffer{create_buffer(
    static_cast<VkDeviceSize>(bytesize),
    VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR,
    VMA_MEMORY_USAGE_CPU_TO_GPU,
    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
  )};
  // Map host data to device.
  if (host_data != nullptr) {
    write_buffer(
      staging_buffer,
      host_data,
      (host_data_size > 0u) ? host_data_size : bytesize
    );
  }
  staging_buffers_.push_back(staging_buffer);
  return staging_buffer;
}

// ----------------------------------------------------------------------------

size_t Allocator::write_buffer(
  backend::Buffer const& dst_buffer,
  size_t const dst_offset,
  void const* host_data,
  size_t const host_offset,
  size_t const bytesize
) const {
  LOG_CHECK( host_data != nullptr );
  LOG_CHECK( dst_buffer.valid() );
  LOG_CHECK( bytesize > 0 );

  void *device_data = nullptr;
  CHECK_VK( vmaMapMemory(handle_, dst_buffer.allocation, &device_data) );

  memcpy(static_cast<char*>(device_data) + dst_offset,
         static_cast<const char*>(host_data) + host_offset, bytesize);

  vmaUnmapMemory(handle_, dst_buffer.allocation);

  return dst_offset + bytesize;
}

// ----------------------------------------------------------------------------

void Allocator::clear_staging_buffers() const {
  for (auto const& staging_buffer : staging_buffers_) {
    destroy_buffer(staging_buffer);
  }
  staging_buffers_.clear();
}

// ----------------------------------------------------------------------------

backend::Image Allocator::create_image(
  VkImageCreateInfo const& image_info,
  VkImageViewCreateInfo view_info,
  VmaMemoryUsage memory_usage
) const {
  LOG_CHECK( view_info.format == image_info.format );
  LOG_CHECK( image_info.format != VK_FORMAT_UNDEFINED );
  LOG_CHECK( image_info.extent.width > 0 && image_info.extent.height > 0 );

  backend::Image image{};

  VmaAllocationCreateInfo const alloc_create_info{
    .usage = memory_usage, //
  };
  VmaAllocationInfo alloc_info{};

  CHECK_VK(vmaCreateImage(
    handle_,
    &image_info,
    &alloc_create_info,
    &image.image,
    &image.allocation,
    &alloc_info
  ));
  image.format = image_info.format;

  view_info.image = image.image;
  CHECK_VK(vkCreateImageView(device_, &view_info, nullptr, &image.view));

  return image;
}

// ----------------------------------------------------------------------------

void Allocator::destroy_image(backend::Image &image) const {
  if (!image.valid()) {
    return;
  }
  vmaDestroyImage(handle_, image.image, image.allocation);
  image.image = VK_NULL_HANDLE;
  if (image.view != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, image.view, nullptr);
    image.view = VK_NULL_HANDLE;
  }
}

/* -------------------------------------------------------------------------- */

} // namespace "backend"
