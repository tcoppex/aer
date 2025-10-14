#include "aer/platform/vulkan/utils.h"

#include <filesystem>
#include <vulkan/vk_enum_string_helper.h>

#include "aer/core/utils.h"
#include "aer/core/logger.h"

/* -------------------------------------------------------------------------- */

namespace vk::utils {

VkShaderModule CreateShaderModule(
  VkDevice device,
  char const* shader_directory,
  char const* shader_name
) {
  /*
  * Note:
  * Since maintenance5, shader module creation can be bypassed if
  * VkShaderModuleCreateInfo is passed to the VkPipelineShaderStageCreateInfo.
  *
  * https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#VkShaderModule
  */

  namespace fs = std::filesystem;
  fs::path spirv_path = fs::path(shader_directory).empty()
                          ? fs::path(shader_name).concat(".spv")
                          : fs::path(shader_directory) / (std::string(shader_name) + ".spv")
                          ;

  auto const filename{spirv_path.string()};

  ::utils::FileReader reader;
  if (!reader.read(filename)) {
    LOG_FATAL("The spirv shader \"{}\" could not be found.\n", spirv_path.string());
  }

  VkShaderModuleCreateInfo shader_module_info{
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = reader.buffer.size(),
    .pCode = reinterpret_cast<uint32_t*>(reader.buffer.data()),
  };

  VkShaderModule module{};
  CHECK_VK( vkCreateShaderModule(device, &shader_module_info, nullptr, &module) );
  SetDebugObjectName(device, module, shader_name);

  return module;
}

// ----------------------------------------------------------------------------

VkResult CheckVKResult(VkResult result, char const* file, int line, bool bExitOnFail) {
  if (VK_SUCCESS != result) {
    LOGE("Vulkan error @ \"{}\" [{}] : [{}].\n", file, line, string_VkResult(result));
    if (bExitOnFail) {
      exit(EXIT_FAILURE);
    }
  }
  return result;
}

// ----------------------------------------------------------------------------

bool IsValidStencilFormat(VkFormat format) {
  // return FormatIsDepthAndStencil(format);
  return (format == VK_FORMAT_D16_UNORM_S8_UINT)
      || (format == VK_FORMAT_D24_UNORM_S8_UINT)
      || (format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      ;
}

// ----------------------------------------------------------------------------

std::tuple<VkPipelineStageFlags2, VkAccessFlags2> MakePipelineStageAccessTuple(
  VkImageLayout state
) {
  switch(state) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      return std::make_tuple( VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                              VK_ACCESS_2_NONE);

    case VK_IMAGE_LAYOUT_GENERAL:
      return std::make_tuple( VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_MEMORY_READ_BIT
                            | VK_ACCESS_2_MEMORY_WRITE_BIT
                            | VK_ACCESS_2_TRANSFER_WRITE_BIT);

    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
      return std::make_tuple( VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT
                            | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
      return std::make_tuple( VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT,
                              VK_ACCESS_2_SHADER_READ_BIT);

    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return std::make_tuple( VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_READ_BIT);

    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return std::make_tuple( VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                              VK_ACCESS_2_TRANSFER_WRITE_BIT);

    // VK_KHR_swapchain

    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return std::make_tuple( VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                              VK_ACCESS_2_NONE);
    // VK_VERSION_1_3

    case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
      return std::make_tuple( VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                            | VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT
                            | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                            | VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                              VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                            | VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT
                            | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

    default: {
      LOGD("{}: Unknown enum {}.", __FUNCTION__, string_VkImageLayout(state));
      return std::make_tuple(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                             VK_ACCESS_2_MEMORY_READ_BIT
                           | VK_ACCESS_2_MEMORY_WRITE_BIT);
    }
  }
}

// ----------------------------------------------------------------------------

void TransformDescriptorSetWriteEntries(
  VkDescriptorSet descriptor_set,
  std::vector<DescriptorSetWriteEntry> const& entries,
  DescriptorSetWriteEntry::Result &result
) {
  // (we do "need" to update some internal value withing that ref..)
  auto& updated_entries{
    const_cast<std::vector<DescriptorSetWriteEntry>&>(entries)
  };

  // (if one update need many of same extensions it could fails).
  auto &ext = result.ext;
  auto& write_descriptor_sets = result.write_descriptor_sets;

  write_descriptor_sets.reserve(entries.size());

  for (auto& entry : updated_entries) {
    VkWriteDescriptorSet write_descriptor_set{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = descriptor_set,
      .dstBinding = entry.binding,
      .dstArrayElement = 0u,
      .descriptorType = entry.type,
    };

    switch (entry.type) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        LOG_CHECK(entry.buffers.empty() && entry.bufferViews.empty());

        write_descriptor_set.pImageInfo = entry.images.data();
        write_descriptor_set.descriptorCount = static_cast<uint32_t>(entry.images.size());
      break;

      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
        LOG_CHECK(entry.images.empty() && entry.bufferViews.empty());
        for (auto &buf : entry.buffers) {
          buf.range = (buf.range == 0) ? VK_WHOLE_SIZE : buf.range;
        }
        write_descriptor_set.pBufferInfo = entry.buffers.data();
        write_descriptor_set.descriptorCount = static_cast<uint32_t>(entry.buffers.size());
      break;

      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        LOG_CHECK(entry.images.empty() && entry.buffers.empty());
        write_descriptor_set.pTexelBufferView = entry.bufferViews.data();
        write_descriptor_set.descriptorCount = static_cast<uint32_t>(entry.bufferViews.size());
      break;

      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        ext.accelerationStructureInfo = {
          .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
          .accelerationStructureCount = static_cast<uint32_t>(entry.accelerationStructures.size()),
          .pAccelerationStructures = entry.accelerationStructures.data(),
        };
        write_descriptor_set.descriptorCount = 1;
        write_descriptor_set.pNext = &ext.accelerationStructureInfo;
      break;

      default:
        LOGE("Unknown descriptor type: {}", static_cast<int>(entry.type));
        continue;
    }

    write_descriptor_sets.push_back(write_descriptor_set);
  }
}

// ----------------------------------------------------------------------------

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDebugMessage(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
  VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData,
  void* pUserData
) {
  std::string const errorTypeString = string_VkDebugUtilsMessageTypeFlagsEXT(messageTypes);

  switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
      LOGE("[{}]\n{}\n", errorTypeString.c_str(), pCallbackData->pMessage);
      break;

    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      LOGW("[{}]\n{}\n", errorTypeString.c_str(), pCallbackData->pMessage);
      break;

    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      LOGI("[{}]\n{}\n", errorTypeString.c_str(), pCallbackData->pMessage);
      break;

    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
      LOGD("[{}]\n{}\n", errorTypeString.c_str(), pCallbackData->pMessage);
      break;

    default:
      LOGD("[{}]\n{}\n", errorTypeString.c_str(), pCallbackData->pMessage);
      break;
  }
  return VK_FALSE;
}

// ----------------------------------------------------------------------------

} // namespace "vkutils"

/* -------------------------------------------------------------------------- */
