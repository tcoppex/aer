#ifndef AER_PLATFORM_BACKEND_VKUTILS_H
#define AER_PLATFORM_BACKEND_VKUTILS_H

/* -------------------------------------------------------------------------- */

#include "aer/core/common.h"
#include "aer/platform/backend/types.h"

#include "volk.h" // load core vulkan + extensions.

/* -------------------------------------------------------------------------- */

#ifdef NDEBUG
# define CHECK_VK(res)  res
#else
# define CHECK_VK(res)  vkutils::CheckVKResult(res, __FILE__, __LINE__, true)
#endif

/* -------------------------------------------------------------------------- */

namespace vkutils {

VkResult CheckVKResult(
  VkResult result,
  char const* file,
  int line,
  bool bExitOnFail
);

bool IsValidStencilFormat(
  VkFormat format
);

VkShaderModule CreateShaderModule(
  VkDevice device,
  char const* shader_directory,
  char const* shader_name
);

// (from nvpro_sample/minimal_latest)
std::tuple<VkPipelineStageFlags2, VkAccessFlags2> MakePipelineStageAccessTuple(
  VkImageLayout state
);

void TransformDescriptorSetWriteEntries(
  VkDescriptorSet descriptor_set,
  std::vector<DescriptorSetWriteEntry> const& entries,
  DescriptorSetWriteEntry::Result &result
);

// ----------------------------------------------------------------------------

constexpr uint32_t GetKernelGridDim(uint32_t numCells, uint32_t blockDim) {
  return (numCells + blockDim - 1u) / blockDim;
}

// ----------------------------------------------------------------------------

template <typename T, typename N>
void PushNextVKStruct(T* baseStruct, N* nextStruct) {
  nextStruct->pNext = baseStruct->pNext;
  baseStruct->pNext = nextStruct;
}

// (from nvpro_sample/minimal_latest)
template <typename T>
constexpr VkObjectType GetObjectType() {
  if constexpr(std::is_same_v<T, VkBuffer>)
    return VK_OBJECT_TYPE_BUFFER;
  else if constexpr(std::is_same_v<T, VkBufferView>)
    return VK_OBJECT_TYPE_BUFFER_VIEW;
  else if constexpr(std::is_same_v<T, VkCommandBuffer>)
    return VK_OBJECT_TYPE_COMMAND_BUFFER;
  else if constexpr(std::is_same_v<T, VkCommandPool>)
    return VK_OBJECT_TYPE_COMMAND_POOL;
  else if constexpr(std::is_same_v<T, VkDescriptorPool>)
    return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
  else if constexpr(std::is_same_v<T, VkDescriptorSet>)
    return VK_OBJECT_TYPE_DESCRIPTOR_SET;
  else if constexpr(std::is_same_v<T, VkDescriptorSetLayout>)
    return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
  else if constexpr(std::is_same_v<T, VkDevice>)
    return VK_OBJECT_TYPE_DEVICE;
  else if constexpr(std::is_same_v<T, VkDeviceMemory>)
    return VK_OBJECT_TYPE_DEVICE_MEMORY;
  else if constexpr(std::is_same_v<T, VkFence>)
    return VK_OBJECT_TYPE_FENCE;
  else if constexpr(std::is_same_v<T, VkFramebuffer>)
    return VK_OBJECT_TYPE_FRAMEBUFFER;
  else if constexpr(std::is_same_v<T, VkImage>)
    return VK_OBJECT_TYPE_IMAGE;
  else if constexpr(std::is_same_v<T, VkImageView>)
    return VK_OBJECT_TYPE_IMAGE_VIEW;
  else if constexpr(std::is_same_v<T, VkInstance>)
    return VK_OBJECT_TYPE_INSTANCE;
  else if constexpr(std::is_same_v<T, VkPipeline>)
    return VK_OBJECT_TYPE_PIPELINE;
  else if constexpr(std::is_same_v<T, VkPipelineCache>)
    return VK_OBJECT_TYPE_PIPELINE_CACHE;
  else if constexpr(std::is_same_v<T, VkPipelineLayout>)
    return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
  else if constexpr(std::is_same_v<T, VkQueryPool>)
    return VK_OBJECT_TYPE_QUERY_POOL;
  else if constexpr(std::is_same_v<T, VkRenderPass>)
    return VK_OBJECT_TYPE_RENDER_PASS;
  else if constexpr(std::is_same_v<T, VkSampler>)
    return VK_OBJECT_TYPE_SAMPLER;
  else if constexpr(std::is_same_v<T, VkSemaphore>)
    return VK_OBJECT_TYPE_SEMAPHORE;
  else if constexpr(std::is_same_v<T, VkShaderModule>)
    return VK_OBJECT_TYPE_SHADER_MODULE;
  else if constexpr(std::is_same_v<T, VkSurfaceKHR>)
    return VK_OBJECT_TYPE_SURFACE_KHR;
  else if constexpr(std::is_same_v<T, VkSwapchainKHR>)
    return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
  else
    return VK_OBJECT_TYPE_UNKNOWN;
}

template <typename T>
void SetDebugObjectName(VkDevice device, T object, std::string_view name) {
  constexpr VkObjectType objectType = vkutils::GetObjectType<T>();

  if (vkSetDebugUtilsObjectNameEXT && (objectType != VK_OBJECT_TYPE_UNKNOWN)) {
    VkDebugUtilsObjectNameInfoEXT info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = objectType,
      .objectHandle = (uint64_t)(object),
      .pObjectName = name.data()
    };
    vkSetDebugUtilsObjectNameEXT(device, &info);
  }
}

// ----------------------------------------------------------------------------

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugMessage(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
  const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
  void* pUserData
);

// ----------------------------------------------------------------------------

} // namespace "vkutils"

/* -------------------------------------------------------------------------- */

#endif // AER_PLATFORM_BACKEND_VKUTILS_H
