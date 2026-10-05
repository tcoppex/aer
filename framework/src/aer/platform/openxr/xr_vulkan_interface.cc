#include <algorithm>

#include "aer/platform/openxr/xr_vulkan_interface.h"

// ----------------------------------------------------------------------------

VkResult XRVulkanInterface::createVulkanInstance(
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

void XRVulkanInterface::getGraphicsDevice(VkPhysicalDevice *physical_device) {
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

VkResult XRVulkanInterface::createVulkanDevice(
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

int64_t XRVulkanInterface::selectColorSwapchainFormat(std::vector<int64_t> const& formats) const {
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

// ----------------------------------------------------------------------------

bool XRVulkanInterface::createSwapchainImages(
  XrSwapchain swapchain,
  XrSwapchainCreateInfo const& info,
  bool use_foveation,
  std::vector<backend::Image>& images,
  std::vector<backend::Image>& fdms
) {
  uint32_t count = 0u;
  CHECK_XR_RET(xrEnumerateSwapchainImages(swapchain, 0, &count, nullptr));
  LOG_CHECK(count > 0u);

  std::vector<XrSwapchainImageVulkanKHR> sc_images(
    count, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR}
  );
  std::vector<XrSwapchainImageFoveationVulkanFB> sc_fdms(
    count, {XR_TYPE_SWAPCHAIN_IMAGE_FOVEATION_VULKAN_FB}
  );
  if (use_foveation) {
    for (uint32_t i = 0u; i < count; ++i) {
      sc_images[i].next = &sc_fdms[i];
    }
  }

  CHECK_XR_RET(xrEnumerateSwapchainImages(
    swapchain, count, &count,
    reinterpret_cast<XrSwapchainImageBaseHeader*>(sc_images.data())
  ));

  auto const aspect =
      (info.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
          ? VK_IMAGE_ASPECT_DEPTH_BIT
          : VK_IMAGE_ASPECT_COLOR_BIT
          ;

  auto view_info = VkImageViewCreateInfo{
    .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType   = (info.arraySize > 1u) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                        : VK_IMAGE_VIEW_TYPE_2D,
    .format     = static_cast<VkFormat>(info.format),
    .components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G,
                   VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A},
    .subresourceRange = {
      .aspectMask = static_cast<VkImageAspectFlags>(aspect),
      .levelCount = 1,
      .layerCount = info.arraySize,
    },
  };

  auto make_views = [&](auto const& src, std::vector<backend::Image>& out) {
    out.clear();
    out.reserve(src.size());
    for (auto const& s : src) {
      view_info.image = s.image;

      VkImageView view{};
      vkCreateImageView(binding_.device, &view_info, nullptr, &view);

      out.push_back(backend::Image{
        .image = s.image,
        .view = view,
        .format = view_info.format
      });
    }
  };

  make_views(sc_images, images);

  fdms.clear();
  if (use_foveation) {
    bool const ok = std::all_of(
      sc_fdms.begin(),
      sc_fdms.end(),
      [](auto const& f) { return f.image != VK_NULL_HANDLE; }
    );

    LOGV("[XR] FDM[0]: image={} size={}x{} ok={}",
         (void*)sc_fdms[0].image, sc_fdms[0].width, sc_fdms[0].height, ok);

    if (ok) {
      view_info.format = VK_FORMAT_R8G8_UNORM;   //
      view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      make_views(sc_fdms, fdms);
    } else {
      LOGW("[XR] Foveation requested but the runtime provided no FDM.");
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

void XRVulkanInterface::destroySwapchainImages(std::vector<backend::Image>& images) {
  for (auto& img : images) {
    if (img.view != VK_NULL_HANDLE) {
      vkDestroyImageView(binding_.device, img.view, nullptr);
    }
  }
  images.clear();
}

// ----------------------------------------------------------------------------

XrResult XRVulkanInterface::xrCreateVulkanInstanceKHR(
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

XrResult XRVulkanInterface::xrGetVulkanGraphicsRequirements2KHR(
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

XrResult XRVulkanInterface::xrGetVulkanGraphicsDevice2KHR(
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

XrResult XRVulkanInterface::xrCreateVulkanDeviceKHR(
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

// ----------------------------------------------------------------------------
