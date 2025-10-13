#ifndef AER_CORE_PLATFORM_OPENXR_XR_VULKAN_INTERFACE_H_
#define AER_CORE_PLATFORM_OPENXR_XR_VULKAN_INTERFACE_H_

#if defined(ANDROID)
#include <jni.h> //
#endif

#include "aer/platform/backend/vulkan_wrapper.h"
#include <openxr/openxr_platform.h>

#include "aer/platform/backend/types.h" // (for backend::Image)
#include "aer/platform/openxr/xr_utils.h"

// ----------------------------------------------------------------------------

struct XRVulkanInterface {
 public:
  XRVulkanInterface(XrInstance instance, XrSystemId system_id)
    : instance_(instance)
    , system_id_(system_id)
  {}

  virtual ~XRVulkanInterface() {}

  VkResult createVulkanInstance(
    VkInstanceCreateInfo const* create_info,
    VkAllocationCallbacks const* allocator,
    VkInstance* vulkanInstance
  ) {
    XrVulkanInstanceCreateInfoKHR info{
      .type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR,
      .systemId = system_id_,
      .pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vulkanCreateInfo = create_info,
      .vulkanAllocator = allocator,
    };
    VkResult vk_result{};
    CHECK_XR(xrCreateVulkanInstanceKHR(
      instance_, &info, vulkanInstance, &vk_result
    ));
    binding_.instance = *vulkanInstance;
    return vk_result;
  }

  void getGraphicsDevice(VkPhysicalDevice *physical_device) {
    XrGraphicsRequirementsVulkan2KHR graphics_requirements{
      XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR
    };
    CHECK_XR(xrGetVulkanGraphicsRequirements2KHR(
      instance_, system_id_, &graphics_requirements
    ));

    XrVulkanGraphicsDeviceGetInfoKHR graphics_device_info{
      .type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR,
      .next = nullptr,
      .systemId = system_id_,
      .vulkanInstance = binding_.instance,
    };
    CHECK_XR(xrGetVulkanGraphicsDevice2KHR(
      instance_, &graphics_device_info, physical_device
    ));
    binding_.physicalDevice = *physical_device;
  }

  VkResult createVulkanDevice(
    VkPhysicalDevice physical_device,
    VkDeviceCreateInfo const* create_info,
    VkAllocationCallbacks const* allocator,
    VkDevice *device
  ) {
    LOG_CHECK(physical_device == binding_.physicalDevice);
    XrVulkanDeviceCreateInfoKHR deviceCreateInfo{
      .type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR,
      .next = nullptr,
      .systemId = system_id_,
      .createFlags = XrVulkanDeviceCreateFlagsKHR{},
      .pfnGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vulkanPhysicalDevice = physical_device,
      .vulkanCreateInfo = create_info,
      .vulkanAllocator = allocator,
    };
    VkResult vk_result{};
    CHECK_XR(xrCreateVulkanDeviceKHR(
      instance_, &deviceCreateInfo, device, &vk_result
    ));
    binding_.device = *device;
    return vk_result;
  }

  void setBindingQueue(uint32_t queue_family_index, uint32_t queue_index) noexcept {
    binding_.queueFamilyIndex = queue_family_index;
    binding_.queueIndex = queue_index;
  }

  [[nodiscard]]
  XrBaseInStructure const* binding() const noexcept {
    return reinterpret_cast<XrBaseInStructure const*>(&binding_);
  }

  [[nodiscard]]
  int64_t selectColorSwapchainFormat(std::vector<int64_t> const& formats) const {
    constexpr std::array<VkFormat, 4> kSupportedColorSwapchainFormats{
      VK_FORMAT_R8G8B8A8_SRGB,
      VK_FORMAT_B8G8R8A8_SRGB,
      VK_FORMAT_R8G8B8A8_UNORM,
      VK_FORMAT_B8G8R8A8_UNORM,
    };
    auto it = std::find_first_of(
      formats.begin(),
      formats.end(),
      kSupportedColorSwapchainFormats.cbegin(),
      kSupportedColorSwapchainFormats.cend()
    );
    if (it == formats.end()) {
      return VK_FORMAT_UNDEFINED;
    }
    return *it;
  }

  void allocateSwapchainImage(
    std::vector<XrSwapchainImageVulkanKHR> const& base_images,
    VkImageViewCreateInfo &view_info,
    std::vector<backend::Image> &images
  ) {
    LOG_CHECK(!base_images.empty());

    images.clear();
    images.reserve(base_images.size());
    for (auto const& base : base_images) {
      view_info.image = base.image;
      VkImageView image_view{};
      vkCreateImageView(binding_.device, &view_info, nullptr, &image_view);
      images.push_back(backend::Image{
        .image = base.image,
        .view = image_view,
        .format = view_info.format,
      });
    }
  }

  void releaseSwapchainImage(std::vector<backend::Image> &images) const noexcept {
    for (auto & img : images) {
      vkDestroyImageView(binding_.device, img.view, nullptr);
    }
    images.clear();
  }

 private:
  XrResult xrCreateVulkanInstanceKHR(
    XrInstance instance,
    XrVulkanInstanceCreateInfoKHR *const createInfo,
    VkInstance* vulkanInstance,
    VkResult* vulkanResult
  ) const {
    PFN_xrCreateVulkanInstanceKHR pfnCreateVulkanInstanceKHR{};
    CHECK_XR(xrGetInstanceProcAddr(
      instance,
      "xrCreateVulkanInstanceKHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateVulkanInstanceKHR)
    ));
    return pfnCreateVulkanInstanceKHR(instance, createInfo, vulkanInstance, vulkanResult);
  }

  XrResult xrGetVulkanGraphicsRequirements2KHR(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsVulkan2KHR* graphicsRequirements
  ) const {
    PFN_xrGetVulkanGraphicsRequirements2KHR pfnGetVulkanGraphicsRequirements2KHR{};
    CHECK_XR(xrGetInstanceProcAddr(
      instance,
      "xrGetVulkanGraphicsRequirements2KHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetVulkanGraphicsRequirements2KHR)
    ));
    return pfnGetVulkanGraphicsRequirements2KHR(instance, systemId, graphicsRequirements);
  }

  XrResult xrGetVulkanGraphicsDevice2KHR(
    XrInstance instance,
    XrVulkanGraphicsDeviceGetInfoKHR *const getInfo,
    VkPhysicalDevice* vulkanPhysicalDevice
  ) const {
    PFN_xrGetVulkanGraphicsDevice2KHR pfnGetVulkanGraphicsDevice2KHR{};
    CHECK_XR(xrGetInstanceProcAddr(
      instance,
      "xrGetVulkanGraphicsDevice2KHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetVulkanGraphicsDevice2KHR)
    ));
    return pfnGetVulkanGraphicsDevice2KHR(instance, getInfo, vulkanPhysicalDevice);
  }

  XrResult xrCreateVulkanDeviceKHR(
    XrInstance instance,
    XrVulkanDeviceCreateInfoKHR *const createInfo,
    VkDevice* vulkanDevice,
    VkResult* vulkanResult
  ) const {
    PFN_xrCreateVulkanDeviceKHR pfnCreateVulkanDeviceKHR{};
    CHECK_XR(xrGetInstanceProcAddr(
      instance,
      "xrCreateVulkanDeviceKHR",
      reinterpret_cast<PFN_xrVoidFunction*>(&pfnCreateVulkanDeviceKHR)
    ));
    return pfnCreateVulkanDeviceKHR(instance, createInfo, vulkanDevice, vulkanResult);
  }

 private:
  XrInstance instance_{XR_NULL_HANDLE};
  XrSystemId system_id_{XR_NULL_SYSTEM_ID};

  XrGraphicsBindingVulkan2KHR binding_{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
};

// ----------------------------------------------------------------------------

#endif // AER_CORE_PLATFORM_OPENXR_XR_VULKAN_INTERFACE_H_
