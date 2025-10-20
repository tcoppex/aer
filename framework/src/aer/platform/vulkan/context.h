#ifndef AER_PLATFORM_VULKAN_CONTEXT_H_
#define AER_PLATFORM_VULKAN_CONTEXT_H_

/* -------------------------------------------------------------------------- */

#include "aer/platform/vulkan/types.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/platform/vulkan/allocator.h"

#include "aer/platform/openxr/xr_vulkan_interface.h" //

/* -------------------------------------------------------------------------- */

class Context {
 public:
  enum class TargetQueue {
    Main,
    Transfer,
    Compute,
    kCount,
  };

 public:
  Context() = default;

  [[nodiscard]]
  bool init(
    std::string_view app_name,
    std::vector<char const*> const& instance_extensions,
    XRVulkanInterface *vulkan_xr
  );

  void release();

  [[nodiscard]]
  VkInstance instance() const noexcept {
    return instance_;
  }

  [[nodiscard]]
  VkPhysicalDevice physical_device() const noexcept {
    return gpu_;
  }

  [[nodiscard]]
  VkDevice device() const noexcept {
    return handle_;
  }

  [[nodiscard]]
  backend::Queue const& queue(
    TargetQueue const target = TargetQueue::Main
  ) const noexcept {
    return queues_[target];
  }

  [[nodiscard]]
  backend::GPUProperties const& properties() const noexcept {
    return properties_;
  }

  [[nodiscard]]
  backend::Allocator const& allocator() const noexcept {
    return allocator_;
  }

  void deviceWaitIdle() const {
    CHECK_VK(vkDeviceWaitIdle(handle_));
  }

  // --- Allocator composition interface --

  [[nodiscard]]
  backend::Buffer createBuffer(
    VkDeviceSize const size,
    VkBufferUsageFlags2KHR const usage,
    VmaMemoryUsage const memory_usage = VMA_MEMORY_USAGE_AUTO,
    VmaAllocationCreateFlags const flags = {}
  ) const {
    return allocator_.createBuffer(size, usage, memory_usage, flags);
  }

  void destroyBuffer(backend::Buffer const& buffer) const {
    allocator_.destroyBuffer(buffer);
  }

  [[nodiscard]]
  backend::Buffer createStagingBuffer(
    size_t const bytesize,
    void const* host_data = nullptr,
    size_t host_data_size = 0u
  ) const {
    return allocator_.createStagingBuffer(bytesize, host_data, host_data_size);
  }

  void clearStagingBuffers() const {
    allocator_.clearStagingBuffers();
  }

  void mapMemory(backend::Buffer const& buffer, void **data) const {
    allocator_.mapMemory(buffer, data);
  }

  void unmapMemory(backend::Buffer const& buffer) const {
    allocator_.unmapMemory(buffer);
  }

  size_t writeBuffer(
    backend::Buffer const& dst_buffer,
    size_t dst_offset,
    void const* host_data,
    size_t host_offset,
    size_t bytesize
  ) const {
    return allocator_.writeBuffer(
      dst_buffer, dst_offset, host_data, host_offset, bytesize
    );
  }

  size_t writeBuffer(
    backend::Buffer const& dst_buffer,
    void const* host_data,
    size_t bytesize
  ) const {
    return allocator_.writeBuffer(dst_buffer, host_data, bytesize);
  }

  template<typename T> requires (!SpanConvertible<T>)
  size_t writeBuffer(
    backend::Buffer const& dst_buffer,
    T const& host_data
  ) const {
    return writeBuffer(dst_buffer, &host_data, sizeof(host_data));
  }

  template<typename T> requires (SpanConvertible<T>)
  size_t writeBuffer(
    backend::Buffer const& dst_buffer,
    T const& host_data
  ) {
    auto const host_span{ std::span(host_data) };
    auto const bytesize{
      sizeof(typename decltype(host_span)::element_type) * host_span.size()
    };
    return writeBuffer(dst_buffer, host_span.data(), bytesize);
  }

  [[nodiscard]]
  backend::Image createImage(
    VkImageCreateInfo const& image_info,
    VkImageViewCreateInfo view_info,
    VmaMemoryUsage memory_usage = VMA_MEMORY_USAGE_GPU_ONLY
  ) const {
    return allocator_.createImage(image_info, view_info, memory_usage);
  }

  void destroyImage(backend::Image &image) const {
    allocator_.destroyImage(image);
  }

  // --- Surface --

  void destroySurface(VkSurfaceKHR surface) const {
    vkDestroySurfaceKHR(instance_, surface, nullptr);
  }

  // --- Image ---

  [[nodiscard]]
  VkSampleCountFlags sample_counts() const noexcept;

  [[nodiscard]]
  VkSampleCountFlagBits max_sample_count() const noexcept;

  [[nodiscard]]
  backend::Image createImage2D(
    uint32_t width,
    uint32_t height,
    uint32_t array_layers,
    uint32_t levels,
    VkFormat format,
    VkSampleCountFlagBits sample_count,
    VkImageUsageFlags usage,
    std::string_view debug_name
  ) const;

  [[nodiscard]]
  backend::Image createImage2D(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    std::string_view debug_name = ""
  ) const {
    return createImage2D(
      width, height, 1u, 1u, format, VK_SAMPLE_COUNT_1_BIT, usage, debug_name
    );
  }

  // --- Shader Module ---

  [[nodiscard]]
  backend::ShaderModule createShaderModule(
    std::string_view directory,
    std::string_view shader_name
  ) const;

  [[nodiscard]]
  backend::ShaderModule createShaderModule(
    std::string_view filepath
  ) const;

  [[nodiscard]]
  std::vector<backend::ShaderModule> createShaderModules(
    std::string_view directory,
    std::vector<std::string_view> const& shader_names
  ) const;

  [[nodiscard]]
  std::vector<backend::ShaderModule> createShaderModules(
    std::vector<std::string_view> const& filepaths
  ) const;

  void releaseShaderModule(
    backend::ShaderModule const& shader
  ) const;

  void releaseShaderModules(
    std::vector<backend::ShaderModule> const& shaders
  ) const;

  // --- Command Pool / Buffer ---

  void resetCommandPool(
    VkCommandPool command_pool
  ) const noexcept;

  void destroyCommandPool(
    VkCommandPool command_pool
  ) const noexcept;

  void freeCommandBuffers(
    VkCommandPool command_pool,
    std::vector<VkCommandBuffer> const& command_buffers
  ) const noexcept;

  void freeCommandBuffer(
    VkCommandPool command_pool,
    VkCommandBuffer command_buffer
  ) const noexcept;

  // --- Transient Command Encoder ---

  [[nodiscard]]
  CommandEncoder createTransientCommandEncoder(
    Context::TargetQueue const& target_queue = TargetQueue::Main
  ) const;

  void finishTransientCommandEncoder(
    CommandEncoder const& encoder
  ) const;

  // --- Transient Command Encoder Wrappers ---

  void transitionImages(
    std::vector<backend::Image> const& images,
    VkImageLayout const src_layout,
    VkImageLayout const dst_layout,
    uint32_t layer_count = 1u
  ) const;

  // (formerly 'createBufferAndUpload')
  [[nodiscard]]
  backend::Buffer transientCreateBuffer(
    void const* host_data,
    size_t host_data_size,
    VkBufferUsageFlags2KHR usage,
    size_t device_buffer_offset = 0u,
    size_t device_buffer_size = 0u
  ) const;

  template<typename T> requires (SpanConvertible<T>)
  [[nodiscard]] backend::Buffer transientCreateBuffer(
    T const& host_data,
    VkBufferUsageFlags2KHR usage,
    size_t device_buffer_offset = 0u,
    size_t device_buffer_size = 0u
  ) const {
    auto const host_span{ std::span(host_data) };
    auto const bytesize{
      sizeof(typename decltype(host_span)::element_type) * host_span.size()
    };
    return transientCreateBuffer(
      host_span.data(), bytesize, usage, device_buffer_offset, device_buffer_size
    );
  }

  void transientUploadBuffer(
    void const* host_data,
    size_t const host_data_size,
    backend::Buffer const& device_buffer,
    size_t const device_buffer_offset = 0u
  ) const;

  template<typename T> requires (SpanConvertible<T>)
  void transientUploadBuffer(
    T const& host_data,
    backend::Buffer const& device_buffer
  ) {
    auto const host_span{ std::span(host_data) };
    auto const bytesize{
      sizeof(typename decltype(host_span)::element_type) * host_span.size()
    };
    transientUploadBuffer(host_span.data(), bytesize, device_buffer);
  }

  void transientCopyBuffer(
    backend::Buffer const& src,
    backend::Buffer const& dst,
    size_t const buffersize
  ) const;

  // --- Descriptor set ---

  void updateDescriptorSet(
    VkDescriptorSet const& descriptor_set,
    std::vector<DescriptorSetWriteEntry> const& entries
  ) const;

  // --- Utils ---

  template <typename T>
  void setDebugObjectName(T object, std::string_view name) const {
#ifndef NDEBUG
    vk_utils::SetDebugObjectName(handle_, object, name);
#endif
  }

 private:
  [[nodiscard]]
  bool has_extension(
    std::string_view name,
    std::vector<VkExtensionProperties> const& extensions
  ) const {
    for (auto const& ext : extensions) {
      if (strcmp(ext.extensionName, name.data()) == 0) {
        return true;
      }
    }
    return false;
  }

  template<typename F>
  bool add_device_feature(
    char const* extension_name,
    F& feature,
    VkStructureType sType,
    std::vector<char const*> const& dependencies = {}
  ) {
    if (!has_extension(extension_name, available_device_extensions_)) {
      LOGI("[Vulkan] Feature extension \"{:s}\" is not available.\n", extension_name);
      return false;
    }
    feature = { .sType = sType };
    vk_utils::PushNextVKStruct(&feature_.base, &feature);
    if (!dependencies.empty()) {
      device_extension_names_.insert(
        device_extension_names_.end(),
        dependencies.begin(),
        dependencies.end()
      );
    }
    device_extension_names_.push_back(extension_name);
    return true;
  }

  void initInstance(
    std::string_view app_name,
    std::vector<char const*> const& instance_extensions
  );

  void selectGPU();

  [[nodiscard]]
  bool initDevice();


 private:
  static constexpr bool kEnableDebugValidationLayer{ true };

  VkDebugUtilsMessengerEXT debug_utils_messenger_{VK_NULL_HANDLE};

  // -----------------------------------------------
  std::vector<VkExtensionProperties> available_device_extensions_{};

  std::vector<char const*> instance_layer_names_{};

  std::vector<char const*> instance_extension_names_{
    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
  };

  std::vector<char const*> device_extension_names_{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME,
    VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
    VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
    VK_KHR_SPIRV_1_4_EXTENSION_NAME,
    VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
  };
  // -----------------------------------------------

  struct {
    VkPhysicalDeviceFeatures2 base{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

    // (to be used with later vulkan version)
    // VkPhysicalDeviceVulkan11Features features11{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    // VkPhysicalDeviceVulkan12Features features12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    // VkPhysicalDeviceVulkan13Features features13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    // VkPhysicalDeviceVulkan14Features features14{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES};

    // VK_VERSION_1_1
    VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT vertex_input_dynamic_state{};
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state{};
    VkPhysicalDeviceExtendedDynamicState2FeaturesEXT extended_dynamic_state2{};
    VkPhysicalDeviceExtendedDynamicState3FeaturesEXT extended_dynamic_state3{};
    VkPhysicalDeviceImageViewMinLodFeaturesEXT image_view_min_lod{};
    VkPhysicalDevice16BitStorageFeaturesKHR storage_16bit{};
    VkPhysicalDeviceMultiviewFeaturesKHR multiview{};
    // VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance1{}; 

    // VK_VERSION_1_2
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT descriptor_indexing{};
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address{};
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timeline_semaphore{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure{};
    // VkPhysicalDeviceRayQueryFeaturesKHR ray_query{};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline{};

    // VK_VERSION_1_3
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering{};
    VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{};
    VkPhysicalDeviceMaintenance4FeaturesKHR maintenance4{};

    // VK_VERSION_1_4
    VkPhysicalDeviceIndexTypeUint8FeaturesEXT index_type_uint8{}; //
    VkPhysicalDeviceMaintenance5FeaturesKHR maintenance5{};
    VkPhysicalDeviceMaintenance6FeaturesKHR maintenance6{};

  } feature_;

  VkInstance instance_{};
  VkPhysicalDevice gpu_{};
  VkDevice handle_{};

  backend::GPUProperties properties_{};

  EnumArray<backend::Queue, TargetQueue> queues_{};
  EnumArray<VkCommandPool, TargetQueue> transient_command_pools_{};

  // --------------------------

  backend::Allocator allocator_{};
  XRVulkanInterface *vulkan_xr_{}; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_PLATFORM_VULKAN_CONTEXT_H_
