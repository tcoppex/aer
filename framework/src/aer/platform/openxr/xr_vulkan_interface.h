#ifndef AER_CORE_PLATFORM_OPENXR_XR_VULKAN_INTERFACE_H_
#define AER_CORE_PLATFORM_OPENXR_XR_VULKAN_INTERFACE_H_

/* -------------------------------------------------------------------------- */

#include "aer/platform/openxr/xr_common.h"

#include "aer/platform/openxr/xr_utils.h"
#include "aer/platform/vulkan/types.h" // (for backend::Image)

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
  );

  void getGraphicsDevice(VkPhysicalDevice *physical_device);

  VkResult createVulkanDevice(
    VkPhysicalDevice physical_device,
    VkDeviceCreateInfo const* create_info,
    VkAllocationCallbacks const* allocator,
    VkDevice *device
  );

  void setBindingQueue(uint32_t queue_family_index, uint32_t queue_index) noexcept {
    binding_.queueFamilyIndex = queue_family_index;
    binding_.queueIndex = queue_index;
  }

  [[nodiscard]]
  XrBaseInStructure const* binding() const noexcept {
    return reinterpret_cast<XrBaseInStructure const*>(&binding_);
  }

  [[nodiscard]]
  int64_t selectColorSwapchainFormat(std::vector<int64_t> const& formats) const;

  [[nodiscard]]
  bool createSwapchainImages(
    XrSwapchain swapchain,
    XrSwapchainCreateInfo const& info,
    bool use_foveation,
    std::vector<backend::Image>& images,
    std::vector<backend::Image>& fdms
  );

  void destroySwapchainImages(std::vector<backend::Image>& images);

  void set_fragment_density_map_supported(bool supported) noexcept {
    fragment_density_map_supported_ = supported;
  }

  [[nodiscard]]
  bool is_fragment_density_map_supported() const noexcept {
    return fragment_density_map_supported_;
  }

 private:
  XrResult xrCreateVulkanInstanceKHR(
    XrInstance instance,
    XrVulkanInstanceCreateInfoKHR *const createInfo,
    VkInstance* vulkanInstance,
    VkResult* vulkanResult
  ) const;

  XrResult xrGetVulkanGraphicsRequirements2KHR(
    XrInstance instance,
    XrSystemId systemId,
    XrGraphicsRequirementsVulkan2KHR* graphicsRequirements
  ) const;

  XrResult xrGetVulkanGraphicsDevice2KHR(
    XrInstance instance,
    XrVulkanGraphicsDeviceGetInfoKHR *const getInfo,
    VkPhysicalDevice* vulkanPhysicalDevice
  ) const;

  XrResult xrCreateVulkanDeviceKHR(
    XrInstance instance,
    XrVulkanDeviceCreateInfoKHR *const createInfo,
    VkDevice* vulkanDevice,
    VkResult* vulkanResult
  ) const;

 private:
  XrInstance instance_{XR_NULL_HANDLE};
  XrSystemId system_id_{XR_NULL_SYSTEM_ID};

  XrGraphicsBindingVulkan2KHR binding_{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};

  bool fragment_density_map_supported_{false};
};

// ----------------------------------------------------------------------------

#endif // AER_CORE_PLATFORM_OPENXR_XR_VULKAN_INTERFACE_H_
