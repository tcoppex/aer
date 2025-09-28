#include "framework/backend/context.h"

#include "framework/backend/vk_utils.h"
#include "framework/core/utils.h" // for ExtractBasename

/* -------------------------------------------------------------------------- */

bool Context::init(std::vector<char const*> const& instance_extensions) {
  CHECK_VK(volkInitialize());

  init_instance(instance_extensions);
  select_gpu();

  if (!init_device()) {
    return false;
  }

  /* Create a transient CommandPool for temporary command buffers. */
  {
    VkCommandPoolCreateInfo command_pool_create_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
    };

    for (uint32_t i = 0u; i < static_cast<uint32_t>(TargetQueue::kCount); ++i) {
      auto const target = static_cast<TargetQueue>(i);
      command_pool_create_info.queueFamilyIndex = queue(target).family_index;
      CHECK_VK(vkCreateCommandPool(
        device_, &command_pool_create_info, nullptr, &transient_command_pools_[target]
      ));
      set_debug_object_name(transient_command_pools_[target],
        "Context::TransientCommandPool::" + std::to_string(i)
      );
    }
  }

  resource_allocator_ = std::make_unique<ResourceAllocator>();
  resource_allocator_->init({
    .physicalDevice = gpu_,
    .device = device_,
    .instance = instance_,
  });

  LOGD("--------------------------------------------\n");

  return true;
}

// ----------------------------------------------------------------------------

void Context::deinit() {
  vkDeviceWaitIdle(device_);

  resource_allocator_->deinit();
  vkDestroyCommandPool(device_, transient_command_pools_[TargetQueue::Main], nullptr); //
  vkDestroyCommandPool(device_, transient_command_pools_[TargetQueue::Transfer], nullptr); //
  vkDestroyCommandPool(device_, transient_command_pools_[TargetQueue::Compute], nullptr); //
  vkDestroyDevice(device_, nullptr);

  vkDestroyDebugUtilsMessengerEXT(instance_, debug_utils_messenger_, nullptr);
  vkDestroyInstance(instance_, nullptr);
}

// ----------------------------------------------------------------------------

backend::Image Context::create_image_2d(
  uint32_t width,
  uint32_t height,
  VkFormat const format,
  VkImageUsageFlags const extra_usage,
  std::string_view debugName
) const {
  VkImageUsageFlags usage{
      VK_IMAGE_USAGE_SAMPLED_BIT
    | extra_usage
  };

  VkImageAspectFlags aspect_mask{ VK_IMAGE_ASPECT_COLOR_BIT };

  // [TODO] check format is a valid depth one too.
  if (vkutils::IsValidStencilFormat(format)) {
    usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT
                | VK_IMAGE_ASPECT_STENCIL_BIT
                ;
  }

  VkImageCreateInfo const image_info{
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent = {
      width,
      height,
      1u
    },
    .mipLevels = 1u, // [todo]
    .arrayLayers = 1u, //
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = usage,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VkImageViewCreateInfo view_info{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .viewType = (image_info.arrayLayers > 1u) ? VK_IMAGE_VIEW_TYPE_2D_ARRAY
                                              : VK_IMAGE_VIEW_TYPE_2D
                                              ,
    .format = image_info.format,
    .components = {
      VK_COMPONENT_SWIZZLE_R,
      VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
    },
    .subresourceRange = {
      .aspectMask = aspect_mask,
      .baseMipLevel = 0u,
      .levelCount = image_info.mipLevels,
      .baseArrayLayer = 0u,
      .layerCount = image_info.arrayLayers,
    },
  };

  backend::Image image{};
  resource_allocator_->create_image_with_view(image_info, view_info, &image);

  set_debug_object_name(
    image.image,
    std::string(debugName.empty() ? "Image2d::NoName" : debugName)
  );

  return image;
}

// ----------------------------------------------------------------------------

backend::ShaderModule Context::create_shader_module(
  std::string_view directory,
  std::string_view shader_name
) const {
  return {
    .module = vkutils::CreateShaderModule(device_, directory.data(), shader_name.data()),
    .basename = utils::ExtractBasename(shader_name, true),
  };
}

// ----------------------------------------------------------------------------

std::vector<backend::ShaderModule> Context::create_shader_modules(
  std::string_view directory, 
  std::vector<std::string_view> const& shader_names
) const {
  std::vector<backend::ShaderModule> shaders{};
  shaders.reserve(shader_names.size());
  for (auto name : shader_names) {
    shaders.push_back(create_shader_module(directory, name));
  }
  return shaders;
}

// ----------------------------------------------------------------------------

backend::ShaderModule Context::create_shader_module(std::string_view filepath) const {
  return create_shader_module("", filepath); //
}

// ----------------------------------------------------------------------------

std::vector<backend::ShaderModule> Context::create_shader_modules(std::vector<std::string_view> const& filepaths) const {
  return create_shader_modules("", filepaths); //
}

// ----------------------------------------------------------------------------

void Context::release_shader_module(backend::ShaderModule const& shader) const {
  vkDestroyShaderModule(device_, shader.module, nullptr);
}

// ----------------------------------------------------------------------------

void Context::release_shader_modules(std::vector<backend::ShaderModule> const& shaders) const {
  for (auto const& shader : shaders) {
    vkDestroyShaderModule(device_, shader.module, nullptr);
  }
}

// ----------------------------------------------------------------------------

CommandEncoder Context::create_transient_command_encoder(Context::TargetQueue const& target_queue) const {
  VkCommandBuffer cmd{};
  VkCommandBufferAllocateInfo const alloc_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
    .commandPool = transient_command_pools_[target_queue],
    .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    .commandBufferCount = 1u,
  };
  CHECK_VK(vkAllocateCommandBuffers(device_, &alloc_info, &cmd));

  auto encoder = CommandEncoder(
    cmd,
    static_cast<uint32_t>(target_queue),
    device_,
    resource_allocator_.get()
  );
  encoder.begin();

  return encoder;
}

// ----------------------------------------------------------------------------

void Context::finish_transient_command_encoder(CommandEncoder const& encoder) const {
  encoder.end();

  VkFenceCreateInfo const fence_info{
    .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
  };
  VkFence fence;
  CHECK_VK( vkCreateFence(device_, &fence_info, nullptr, &fence) );

  VkCommandBufferSubmitInfo const cb_submit_info{
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .commandBuffer = encoder.command_buffer_,
  };
  VkSubmitInfo2 const submit_info_2{
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .commandBufferInfoCount = 1u,
    .pCommandBufferInfos = &cb_submit_info,
  };

  auto const target_queue{
    static_cast<TargetQueue>(encoder.get_target_queue_index())
  };

  CHECK_VK( vkQueueSubmit2(queue(target_queue).queue, 1u, &submit_info_2, fence) );

  CHECK_VK( vkWaitForFences(device_, 1u, &fence, VK_TRUE, UINT64_MAX) );
  vkDestroyFence(device_, fence, nullptr);

  vkFreeCommandBuffers(device_, transient_command_pools_[target_queue], 1u, &encoder.command_buffer_);
}

// ----------------------------------------------------------------------------

void Context::transition_images_layout(
  std::vector<backend::Image> const& images,
  VkImageLayout const src_layout,
  VkImageLayout const dst_layout
) const {
  auto cmd{ create_transient_command_encoder(TargetQueue::Transfer) };
  cmd.transition_images_layout(images, src_layout, dst_layout);
  finish_transient_command_encoder(cmd);
}

// ----------------------------------------------------------------------------

backend::Buffer Context::create_buffer_and_upload(
  void const* host_data,
  size_t const host_data_size,
  VkBufferUsageFlags2KHR const usage,
  size_t device_buffer_offset,
  size_t const device_buffer_size
) const {
  auto cmd{ create_transient_command_encoder(TargetQueue::Transfer) };
  backend::Buffer buffer{
    cmd.create_buffer_and_upload(host_data, host_data_size, usage, device_buffer_offset, device_buffer_size)
  };
  finish_transient_command_encoder(cmd);
  return buffer;
}

// ----------------------------------------------------------------------------

void Context::transfer_host_to_device(
  void const* host_data,
  size_t const host_data_size,
  backend::Buffer const& device_buffer,
  size_t const device_buffer_offset
) const {
  auto cmd{ create_transient_command_encoder(TargetQueue::Transfer) };
  cmd.transfer_host_to_device(host_data, host_data_size, device_buffer, device_buffer_offset);
  finish_transient_command_encoder(cmd);
}

// ----------------------------------------------------------------------------

void Context::copy_buffer(
  backend::Buffer const& src,
  backend::Buffer const& dst,
  size_t const buffersize
) const {
  auto cmd{ create_transient_command_encoder(Context::TargetQueue::Transfer) };
  cmd.copy_buffer(src, dst, buffersize);
  finish_transient_command_encoder(cmd);
}

// ----------------------------------------------------------------------------

void Context::update_descriptor_set(
  VkDescriptorSet const& descriptor_set,
  std::vector<DescriptorSetWriteEntry> const& entries
) const {
  if (entries.empty()) {
    return;
  }

  DescriptorSetWriteEntry::Result result{};
  vkutils::TransformDescriptorSetWriteEntries(descriptor_set, entries, result);

  vkUpdateDescriptorSets(
    device_,
    static_cast<uint32_t>(result.write_descriptor_sets.size()),
    result.write_descriptor_sets.data(),
    0u,
    nullptr
  );
}

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

void Context::init_instance(std::vector<char const*> const& instance_extensions) {
  std::vector<VkLayerProperties> available_instance_layers{};
  std::vector<VkExtensionProperties> available_instance_extensions{};

  uint32_t layerCount = 0;
  CHECK_VK( vkEnumerateInstanceLayerProperties(&layerCount, nullptr) );
  available_instance_layers.resize(layerCount);
  CHECK_VK( vkEnumerateInstanceLayerProperties(&layerCount, available_instance_layers.data()) );

#ifndef NDEBUG
  auto hasLayer = [&](char const* layerName) {
    for (const auto& layer : available_instance_layers) {
      if (std::string(layer.layerName) == std::string(layerName)) {
        return true;
      }
    }
    return false;
  };
  if constexpr (kEnableDebugValidationLayer) {
    if (auto layername = "VK_LAYER_KHRONOS_validation"; hasLayer(layername)) {
      instance_layer_names_.push_back(layername);
    }
  }
#endif

  instance_extension_names_.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  VkDebugUtilsMessengerCreateInfoEXT debug_info{
    .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
    .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                     | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
#ifndef NDEBUG
                     // | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT
                     // | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
#endif
                     ,
    .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                 | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                 | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT
                 ,
    .pfnUserCallback = vkutils::VulkanDebugMessage,
    .pUserData = this,
  };

  // ------------------------------------------

  uint32_t extension_count{0u};
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
  available_instance_extensions.resize(extension_count);
  vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available_instance_extensions.data());

  // Add extensions requested by the application.
  instance_extension_names_.insert(
    instance_extension_names_.begin(), instance_extensions.begin(), instance_extensions.end()
  );

  VkApplicationInfo const application_info{
    .pApplicationName = "hello_vulkan_sample_app", //
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName = "vk_framework",
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_1,
  };

  VkInstanceCreateInfo const instance_create_info{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
#ifndef NDEBUG
    .pNext = &debug_info,
#endif
    .pApplicationInfo = &application_info,
    .enabledLayerCount = static_cast<uint32_t>(instance_layer_names_.size()),
    .ppEnabledLayerNames = instance_layer_names_.data(),
    .enabledExtensionCount = static_cast<uint32_t>(instance_extension_names_.size()),
    .ppEnabledExtensionNames = instance_extension_names_.data(),
  };
  CHECK_VK( vkCreateInstance(&instance_create_info, nullptr, &instance_) );

  volkLoadInstance(instance_);

  // ------------------------------------------

  CHECK_VK(vkCreateDebugUtilsMessengerEXT(instance_, &debug_info, nullptr, &debug_utils_messenger_));

#ifndef NDEBUG
  LOGD("Vulkan version requested: {}.{}.{}",
    VK_API_VERSION_MAJOR(application_info.apiVersion),
    VK_API_VERSION_MINOR(application_info.apiVersion),
    VK_API_VERSION_PATCH(application_info.apiVersion)
  );
  LOGD(" ");

  if (!available_instance_layers.empty()) {
    LOGD("Available Instance layers:");
    for (const auto& layer : available_instance_layers) {
      LOGD(" > {}", layer.layerName);
    }
    LOGD(" ");
  }

  if (!instance_extension_names_.empty()) {
    LOGD("Used Instance extensions:");
    for (auto const& name : instance_extension_names_) {
      LOGD(" > {}", name);
    }
    LOGD(" ");
  }
#endif
}

// ----------------------------------------------------------------------------

void Context::select_gpu() {
  uint32_t gpu_count{0u};
  CHECK_VK( vkEnumeratePhysicalDevices(instance_, &gpu_count, nullptr) );
  if (0u == gpu_count) {
    LOG_FATAL("Vulkan: no GPUs were available.\n");
  }
  std::vector<VkPhysicalDevice> gpus(gpu_count);
  CHECK_VK( vkEnumeratePhysicalDevices(instance_, &gpu_count, gpus.data()) );

  /* Search for a discrete GPU. */
  uint32_t selected_index{0u};
  VkPhysicalDeviceProperties2 props{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  for (uint32_t i = 0u; i < gpu_count; ++i) {
    vkGetPhysicalDeviceProperties2(gpus[i], &props);
    if (VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU == props.properties.deviceType) {
      selected_index = i;
      break;
    }
  }
  gpu_ = gpus[selected_index];

  /* Retrieve differents GPU properties. */
  vkGetPhysicalDeviceProperties2(gpu_, &properties_.gpu2);
  vkGetPhysicalDeviceMemoryProperties2(gpu_, &properties_.memory2);
  uint32_t queue_family_count{0u};
  vkGetPhysicalDeviceQueueFamilyProperties2(gpu_, &queue_family_count, nullptr);
  properties_.queue_families2.resize(queue_family_count, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
  vkGetPhysicalDeviceQueueFamilyProperties2(gpu_, &queue_family_count, properties_.queue_families2.data());

#ifndef NDEBUG
  LOGD("Selected Device:");
  LOGD(" - Device Name    : {}", properties_.gpu2.properties.deviceName);
  LOGD(" - Driver version : {}.{}.{}",
    VK_API_VERSION_MAJOR(properties_.gpu2.properties.driverVersion),
    VK_API_VERSION_MINOR(properties_.gpu2.properties.driverVersion),
    VK_API_VERSION_PATCH(properties_.gpu2.properties.driverVersion)
  );
  LOGD(" - API version    : {}.{}.{}",
    VK_API_VERSION_MAJOR(properties_.gpu2.properties.apiVersion),
    VK_API_VERSION_MINOR(properties_.gpu2.properties.apiVersion),
    VK_API_VERSION_PATCH(properties_.gpu2.properties.apiVersion)
  );
  LOGD(" ");
#endif
}

// ----------------------------------------------------------------------------

bool Context::init_device() {
  /* Retrieve availables device extensions. */
  uint32_t extension_count{0u};
  CHECK_VK(vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &extension_count, nullptr));
  available_device_extensions_.resize(extension_count);
  CHECK_VK(vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &extension_count, available_device_extensions_.data()));

#ifndef NDEBUG
  // for (auto const& prop : available_device_extensions_) {
  //   LOGI("{}", prop.extensionName);
  // }
#endif

  /* Vulkan GPU features. */
  {
    add_device_feature(
      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
      feature_.buffer_device_address,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR
    );

    add_device_feature(
      VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
      feature_.storage_16bit,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES_KHR
    );

    add_device_feature(
      VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
      feature_.dynamic_rendering,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR
    );

    add_device_feature(
      VK_KHR_MAINTENANCE_4_EXTENSION_NAME,
      feature_.maintenance4,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_4_FEATURES_KHR
    );

    add_device_feature(
      VK_KHR_MAINTENANCE_5_EXTENSION_NAME,
      feature_.maintenance5,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES_KHR
    );

    add_device_feature(
      VK_KHR_MAINTENANCE_6_EXTENSION_NAME,
      feature_.maintenance6,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES_KHR
    );

    add_device_feature(
      VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
      feature_.timeline_semaphore,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES
    );

    // add_device_feature(
    //   VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
    //   feature_.swapchain_maintenance1,
    //   VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT
    // );

    add_device_feature(
      VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
      feature_.synchronization2,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR
    );

    add_device_feature(
      VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
      feature_.descriptor_indexing,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
    );

    add_device_feature(
      VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
      feature_.extended_dynamic_state,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT
    );

    add_device_feature(
      VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME,
      feature_.extended_dynamic_state2,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT
    );

    add_device_feature(
      VK_EXT_EXTENDED_DYNAMIC_STATE_3_EXTENSION_NAME,
      feature_.extended_dynamic_state3,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_3_FEATURES_EXT
    );

    add_device_feature(
      VK_EXT_IMAGE_VIEW_MIN_LOD_EXTENSION_NAME,
      feature_.image_view_min_lod,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_MIN_LOD_FEATURES_EXT
    );

    add_device_feature(
      VK_EXT_INDEX_TYPE_UINT8_EXTENSION_NAME,
      feature_.index_type_uint8,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INDEX_TYPE_UINT8_FEATURES_EXT
    );

    add_device_feature(
      VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME,
      feature_.vertex_input_dynamic_state,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT
    );

    add_device_feature(
      VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
      feature_.acceleration_structure,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
    );

#if !defined(ANDROID)
    add_device_feature(
      VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
      feature_.ray_tracing_pipeline,
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
    );
#endif

    vkGetPhysicalDeviceFeatures2(gpu_, &feature_.base);
  }
  LOG_CHECK(feature_.dynamic_rendering.dynamicRendering);
  LOG_CHECK(feature_.timeline_semaphore.timelineSemaphore);
  LOG_CHECK(feature_.synchronization2.synchronization2);
  LOG_CHECK(feature_.descriptor_indexing.descriptorBindingPartiallyBound);
  LOG_CHECK(feature_.descriptor_indexing.runtimeDescriptorArray);
  LOG_CHECK(feature_.descriptor_indexing.shaderSampledImageArrayNonUniformIndexing);
  LOG_CHECK(feature_.vertex_input_dynamic_state.vertexInputDynamicState);

#if !defined(ANDROID)
  LOG_CHECK(feature_.ray_tracing_pipeline.rayTracingPipeline);
#endif

  // --------------------

  /* Find specific Queues Family */
  std::array<float, 3u> constexpr priorities{
    1.0f,     // MAIN Queue        (Graphics, Transfer, Compute)
    0.75f,    // TRANSFERT Queue   (Transfer)
    0.75f,    // COMPUTE Queue     (Compute)
  };
  std::vector<std::pair<backend::Queue*, VkQueueFlags>> const queues{
    { &queues_[TargetQueue::Main],      VK_QUEUE_GRAPHICS_BIT
                                      | VK_QUEUE_TRANSFER_BIT
                                      | VK_QUEUE_COMPUTE_BIT  },
    { &queues_[TargetQueue::Transfer],  VK_QUEUE_TRANSFER_BIT },
    { &queues_[TargetQueue::Compute],   VK_QUEUE_COMPUTE_BIT  },
  };

  std::vector<VkDeviceQueueCreateInfo> queue_create_infos{};
  std::vector<std::vector<float>> queue_priorities{};
  {
    uint32_t const queue_family_count{static_cast<uint32_t>(properties_.queue_families2.size())};

    std::vector<VkDeviceQueueCreateInfo> queue_infos(queue_family_count, {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 0u,
    });

    queue_priorities.resize(queue_family_count, {});

    for (size_t j = 0u; j < queues.size(); ++j) {
      auto& pair = queues[j];

      for (uint32_t i = 0u; i < queue_family_count; ++i) {
        auto const& queue_family_props = properties_.queue_families2[i].queueFamilyProperties;
        auto const& queue_flags = queue_family_props.queueFlags;

        if ((pair.second == (queue_flags & pair.second))
         && (queue_infos[i].queueCount < queue_family_props.queueCount))
        {
          pair.first->family_index = i;
          pair.first->queue_index = queue_infos[i].queueCount;

          queue_priorities[i].push_back(priorities[j]);

          queue_infos[i].queueFamilyIndex = i;
          queue_infos[i].pQueuePriorities = queue_priorities[i].data();
          queue_infos[i].queueCount += 1u;
          // LOGI("{} {} {}", i, priorities[j], queue_infos[i].queueCount);
          break;
        }
      }

      if (UINT32_MAX == pair.first->family_index) {
        LOGE("Could not find a queue family with the requested support {:08x}.", pair.second);
        return false;
      }
    }

    for (uint32_t i = 0u; i < queue_family_count; ++i) {
      if (queue_infos[i].queueCount > 0u) {
        queue_create_infos.push_back(queue_infos[i]);
      }
    }
  }

  /* Create logical device. */
  VkDeviceCreateInfo const device_info{
    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext = &feature_.base,
    .queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size()),
    .pQueueCreateInfos = queue_create_infos.data(),
    .enabledExtensionCount = static_cast<uint32_t>(device_extension_names_.size()),
    .ppEnabledExtensionNames = device_extension_names_.data(),
  };
  CHECK_VK( vkCreateDevice(gpu_, &device_info, nullptr, &device_) );

  /* Load device extensions. */
  volkLoadDevice(device_);

  /* Use aliases without suffixes. */
  {
    auto bind_func{ [](auto & f1, auto & f2) { if (!f1) { f1 = f2; } } };
    bind_func(         vkWaitSemaphores, vkWaitSemaphoresKHR);
    bind_func(    vkCmdPipelineBarrier2, vkCmdPipelineBarrier2KHR);
    bind_func(           vkQueueSubmit2, vkQueueSubmit2KHR);
    bind_func(      vkCmdBeginRendering, vkCmdBeginRenderingKHR);
    bind_func(        vkCmdEndRendering, vkCmdEndRenderingKHR);
    bind_func(  vkCmdBindVertexBuffers2, vkCmdBindVertexBuffers2EXT);
  }

  /* Retrieved requested queues. */
  for (auto& pair : queues) {
    auto *queue = pair.first;
    vkGetDeviceQueue(device_, queue->family_index, queue->queue_index, &queue->queue);
  }

#ifndef NDEBUG
  LOGD("Used Device Extensions:");
  for (auto const& name : device_extension_names_) {
    LOGD(" > {}", name);
  }
  LOGD(" ");

  set_debug_object_name(queues_[TargetQueue::Main].queue,     "Queue::Main");
  set_debug_object_name(queues_[TargetQueue::Transfer].queue, "Queue::Transfer");
  set_debug_object_name(queues_[TargetQueue::Compute].queue,  "Queue::Compute");
#endif

  return true;
}

/* -------------------------------------------------------------------------- */
