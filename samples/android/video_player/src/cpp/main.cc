/* -------------------------------------------------------------------------- */
//
//    Video Player
//
//  Stream a simple video in XR on a composition layer.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"

#include <chrono>

#include "video_decoder.h"

namespace shader_interop {
#include "../shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class VideoPlayerSample final : public Application {
 private:
  static constexpr uint32_t kLayerRes{ 1024u };

  static constexpr char kVideoURL[]{
    "https://video.blender.org/object-storage/web_videos/bf1f3fb5-b119-4f9f-9930-8e20e892b898-360.mp4"
  };

 public:
  AppSettings settings() const noexcept final {
    AppSettings S{};
    S.renderer.color_format = VK_FORMAT_R8G8B8A8_UNORM;
    return S;
  }

  bool setup() final {
    renderer_.set_clear_color(vec4(0.85f, 0.55f, 0.65f, 0.25f));
    renderer_.set_foveated_rendering_enabled(false);

    // --------------------------

    /* Create a secondary swapchain for the UI panel. */
    ui_swapchain_ = xr_->createCompositionSwapchain(kLayerRes, kLayerRes);
    if (!ui_swapchain_) {
      LOGE("failed to create ui swapchain");
      return false;
    }
    ui_swapchain_ptr_ = ui_swapchain_.get();

    ui_frame_handler_ = {
      .swapchain_pptr = &ui_swapchain_ptr_,
      .frames = context_.createFrameResources(
        *ui_swapchain_,
        {
          .colors = {{
            .format = settings().renderer.color_format,
            .clear_value = {{ 0.15f, 0.65f, 0.85f, 0.75f }}
          }},
          .depth_stencil = {
            .format = context_.default_depth_stencil_format(),
          },
          .size = { kLayerRes, kLayerRes },
          .array_size = 1u,
          .sample_count = VK_SAMPLE_COUNT_1_BIT,
          .debug_prefix = "UI_CompositionLayer_RT",
        }
      ),
    };

    // --------------------------

    {
      if (!video_.decoder.start(kVideoURL)) {
        LOGE("Video decoder start failed.");
        return false;
      }

      bool got = false;
      for (int i = 0; i < 300 && !got; ++i) {
        got = acquireVideoFrame();
        if (!got) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
      }
      if (!got || video_.sampler == VK_NULL_HANDLE) {
        LOGE("Video decode found no first frame.");
        return false;
      }
    }

    // --------------------------

    /* Create buffers. */
    {
      uniform_buffer_ = context_.createBuffer(
        2u * sizeof(shader_interop::UniformCameraData),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU
      );
    }

    /* Create the descriptor set with its binding. */
    {
      LOG_CHECK(video_.sampler != VK_NULL_HANDLE);

      uint32_t const kDescSetUniformBinding = 0u;
      uint32_t const kDescSetVideoBinding = 1u;

      descriptor_set_layout_ = context_.createDescriptorSetLayout(
        {
          {
            .binding = kDescSetUniformBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .bindingFlags =
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
              | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
              | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
          },
          {
            .binding = kDescSetVideoBinding,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1u,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = &video_.sampler,
            .bindingFlags =
                VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
              | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
              | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
          }
        },
        VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
      );

      descriptor_set_ = context_.createDescriptorSet(
        descriptor_set_layout_,
        {{
          .binding = kDescSetUniformBinding,
          .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          .buffers = {{ uniform_buffer_.buffer }}
        }}
      );
    }

    auto const shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "main.slang");

    pipeline_layout_ = context_.createPipelineLayout({
      .setLayouts = { descriptor_set_layout_ },
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(shader_interop::PushConstant),
        }
      },
    });
    graphics_pipeline_ = context_.createGraphicsPipeline(
      pipeline_layout_,
      {
        .vertex = {
          .module = shader.handle,
          .entryPoint = "vertexMain",
        },
        .fragment = {
          .module = shader.handle,
          .entryPoint = "fragmentMain",
          .targets = {
            {
              .writeMask = VK_COLOR_COMPONENT_R_BIT
                         | VK_COLOR_COMPONENT_G_BIT
                         | VK_COLOR_COMPONENT_B_BIT
                         | VK_COLOR_COMPONENT_A_BIT
                         ,
            }
          },
        },
        // [issue when setup... incorrect values ?]
        // .depthStencil = {
        //   .depthTestEnable = VK_TRUE,
        //   .depthWriteEnable = VK_TRUE,
        //   .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        // },
        .primitive = {
          .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        }
      }
    );

    context_.releaseShaderModule(shader);

    // ----------------------------

    return true;
  }

  void release() final {
    video_.decoder.stop();
    releaseVideoFrame(true);

    context_.destroyResources(
      descriptor_set_layout_,
      pipeline_layout_,
      graphics_pipeline_,
      uniform_buffer_
    );

    context_.releaseFrameResources(ui_frame_handler_.frames);
    ui_swapchain_.reset();
  }

  void update(float dt) final {
    auto const& frame = xr_->frame_data();
    auto const& input = xr_->frame_control_state();

    if (input.button_thumbstick[XRSide::Right]) {
      video_.decoder.set_pause(!video_.decoder.is_paused());
    }

    // Update uniform buffer for both eyes.
    std::vector<shader_interop::UniformCameraData> camera_data{
      {
        .projectionMatrix = frame.projMatrices[0],
        .viewMatrix = frame.viewMatrices[0]
      },
      {
        .projectionMatrix = frame.projMatrices[1],
        .viewMatrix = frame.viewMatrices[1]
      },
    };
    context_.writeBuffer(uniform_buffer_, camera_data);

    // ---------------------

    updateUILayer(); //
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
    }
    cmd.endRendering();
  }

 private:
  void updateUILayer() {
    if (!ui_swapchain_) {
      LOGW("UI swapchain not setup!");
      return;
    }

    acquireVideoFrame();

    auto &cmd = renderer_.beginFrame(&ui_frame_handler_);
    {
      auto pass = cmd.beginRendering();
      {
        pass.bindPipeline(graphics_pipeline_);
        pass.bindDescriptorSet(descriptor_set_,
            VK_SHADER_STAGE_VERTEX_BIT
          | VK_SHADER_STAGE_FRAGMENT_BIT
        );
        pass.pushConstant(push_constant_, VK_SHADER_STAGE_VERTEX_BIT);
        pass.draw(3);
      }
      cmd.endRendering();
    }
    if (renderer_.endFrame()) {
      xr_->pushCompositionLayer({.quad = {
        .type = XR_TYPE_COMPOSITION_LAYER_QUAD,
        .layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT,
        .space = xr_->get_space(XRSpaceId::Head),
        .subImage = {
          .swapchain = ui_swapchain_->handle(),
          .imageRect = ui_swapchain_->rect(),
          .imageArrayIndex = 0
        },
        .pose = {
          .orientation = {0.0f, 0.0f, 0.0f, 1.0f},
          .position = {0.0f, 0.0f, -1.0f}
        },
        .size = {0.8f, 0.45f},
      }});
    }
  }

  bool acquireVideoFrame() {
    auto &frame = video_.frame;

    AImage *img = nullptr;
    auto hw_buffer = video_.decoder.acquireLatestFrame(&img);
    if (!hw_buffer || !img) {
      return false;
    }

    releaseVideoFrame();
    frame.android_image = img;

    if (!importHardwareBufferToVulkan(hw_buffer)) {
      LOGE("importHardwareBufferToVulkan returned false");
      return false;
    }

    if (descriptor_set_ != VK_NULL_HANDLE) {
      context_.updateDescriptorSet(descriptor_set_, {{
        .binding = 1u,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = {
          {
            .imageView = frame.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          }
        }
      }});
    }

    return true;
  }

  void releaseVideoFrame(bool releaseAll = false) {
    auto const device = context_.device();

    if (releaseAll) {
      if (video_.ycbcr_conversion != VK_NULL_HANDLE) {
        vkDestroySamplerYcbcrConversion(device, video_.ycbcr_conversion, nullptr);
      }
      if (video_.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, video_.sampler, nullptr);
      }
    }

    auto &frame = video_.frame;
    if (frame.android_image != nullptr) {
      vkDestroyImageView(device, frame.view, nullptr);
      vkDestroyImage(device, frame.image, nullptr);
      vkFreeMemory(device, frame.memory, nullptr);
      video_.decoder.releaseImage(frame.android_image);
      frame = {};
    }
  }

  bool importHardwareBufferToVulkan(AHardwareBuffer* hw_buffer) {
    VkDevice const device = context_.device();
    uint32_t const width  = video_.decoder.width();
    uint32_t const height = video_.decoder.height();

    auto &frame = video_.frame;

    if (!hw_buffer) {
      LOGE("importHardwareBufferToVulkan: android hw buffer is empty!");
      return false;
    }

    if (!video_.getAndroidHardwareBufferPropertiesANDROID) {
      video_.getAndroidHardwareBufferPropertiesANDROID = reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
        vkGetDeviceProcAddr(device, "vkGetAndroidHardwareBufferPropertiesANDROID")
      );
      if (!video_.getAndroidHardwareBufferPropertiesANDROID) {
        LOGE("importHardwareBufferToVulkan: vkGetAndroidHardwareBufferPropertiesANDROID not found");
        return false;
      }
    }

    // Retrieve the video format.
    auto fmt_properties = VkAndroidHardwareBufferFormatPropertiesANDROID{
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID,
      .pNext = nullptr,
    };
    auto properties = VkAndroidHardwareBufferPropertiesANDROID{
      .sType = VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID,
      .pNext = &fmt_properties,
    };
    auto const res_props = video_.getAndroidHardwareBufferPropertiesANDROID(
      device, hw_buffer, &properties
    );
    if (res_props != VK_SUCCESS) {
      LOGE("importHardwareBufferToVulkan: Failed to get hardware buffer properties: {}", (uint32_t)res_props);
      return false;
    }

    bool const has_external_format = (fmt_properties.externalFormat != 0);

    VkFormat vk_format = has_external_format ? VK_FORMAT_UNDEFINED
                                             : fmt_properties.format
                                             ;
    if (!has_external_format && vk_format == VK_FORMAT_UNDEFINED) {
      vk_format = VK_FORMAT_R8G8B8A8_UNORM;
    }

    bool const is_yuv = has_external_format
                    || (vk_format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM)
                    || (vk_format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM)
                    || (vk_format == VK_FORMAT_G8_B8R8_2PLANE_422_UNORM)
                    || (vk_format == VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM)
                    ;

    auto const external_format = VkExternalFormatANDROID{
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID,
      .pNext = nullptr,
      .externalFormat = fmt_properties.externalFormat,
    };

    // Create Image.
    {
      auto const external_image_info = VkExternalMemoryImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = has_external_format ? &external_format : nullptr,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID,
      };
      auto const image_info = VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &external_image_info,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = vk_format,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      };
      auto const res_image = vkCreateImage(
        device, &image_info, nullptr, &frame.image
      );
      if (res_image != VK_SUCCESS) {
        LOGE("importHardwareBufferToVulkan: Failed to create image: {}", (uint32_t)res_image);
        vkFreeMemory(device, frame.memory, nullptr);
        frame.memory = VK_NULL_HANDLE;
        return false;
      }
    }

    // Alloc memory.
    {
      auto const import_info = VkImportAndroidHardwareBufferInfoANDROID{
        .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
        .pNext = nullptr,
        .buffer = hw_buffer,
      };
      uint32_t memory_type_index = context_.findMemoryType(
        properties.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
      );
      if (memory_type_index == UINT32_MAX) {
        memory_type_index = context_.findMemoryType(properties.memoryTypeBits, 0);
      }
      if (memory_type_index == UINT32_MAX) {
        LOGE("importHardwareBufferToVulkan: Failed to find any suitable memory type.");
        return false;
      }

      auto const alloc_info = VkMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_info,
        .allocationSize = properties.allocationSize,
        .memoryTypeIndex = memory_type_index,
      };
      auto const res_alloc = vkAllocateMemory(
        device, &alloc_info, nullptr, &frame.memory
      );
      if (res_alloc != VK_SUCCESS || frame.memory == VK_NULL_HANDLE) {
        LOGE("importHardwareBufferToVulkan: Failed to allocate memory: {}", (uint32_t)res_alloc);
        return false;
      }
    }

    // Bind memory.
    {
      auto const res_bind = vkBindImageMemory(device, frame.image, frame.memory, 0);
      if (res_bind != VK_SUCCESS) {
        LOGE("importHardwareBufferToVulkan: Failed to bind image memory: {}", (uint32_t)res_bind);
        vkDestroyImage(device, frame.image, nullptr);
        vkFreeMemory(device, frame.memory, nullptr);
        frame.image = VK_NULL_HANDLE;
        frame.memory = VK_NULL_HANDLE;
        return false;
      }
    }

    // -----

    // Create the sampler (with YUV conversion when needed).
    if (video_.sampler == VK_NULL_HANDLE) {
      if (is_yuv) {
        VkSamplerYcbcrConversionCreateInfo ycbcr_create_info{
          .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
          .pNext = has_external_format ? &external_format : nullptr,
          .format = vk_format,
          .ycbcrModel = fmt_properties.suggestedYcbcrModel,
          .ycbcrRange = fmt_properties.suggestedYcbcrRange,
          .components = fmt_properties.samplerYcbcrConversionComponents,
          .xChromaOffset = fmt_properties.suggestedXChromaOffset,
          .yChromaOffset = fmt_properties.suggestedYChromaOffset,
          .chromaFilter = VK_FILTER_LINEAR,
          .forceExplicitReconstruction = VK_FALSE,
        };

        VkResult const ycbcr_result = vkCreateSamplerYcbcrConversion(
          device,
          &ycbcr_create_info,
          nullptr,
          &video_.ycbcr_conversion
        );
        video_.ycbcr_info.conversion = video_.ycbcr_conversion;

        if (ycbcr_result != VK_SUCCESS) {
          LOGE("vkCreateSamplerYcbcrConversion failed: {}", (uint32_t)ycbcr_result);
          return false;
        }
      }

      auto const sampler_info = VkSamplerCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = is_yuv ? &video_.ycbcr_info : nullptr,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .compareEnable = VK_FALSE,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
      };
      auto const sampler_result = vkCreateSampler(
        device,
        &sampler_info,
        nullptr,
        &video_.sampler
      );
      if (sampler_result != VK_SUCCESS) {
        LOGE("vkCreateSampler failed: {}", (uint32_t)sampler_result);
        vkDestroySamplerYcbcrConversion(
          device, video_.ycbcr_conversion, nullptr
        );
        video_.ycbcr_conversion = VK_NULL_HANDLE;
        return false;
      }
    }

    auto const view_info = VkImageViewCreateInfo{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext = (is_yuv && (video_.ycbcr_conversion != VK_NULL_HANDLE)) ? &video_.ycbcr_info : nullptr,
      .image = frame.image,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = vk_format,
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      },
    };
    auto const image_view_result = vkCreateImageView(
      device, &view_info, nullptr, &frame.view
    );

    return (VK_SUCCESS == image_view_result);
  }

 private:
  std::shared_ptr<OpenXRSwapchain> ui_swapchain_{};
  SwapchainInterface *ui_swapchain_ptr_{};
  Renderer::FrameHandler ui_frame_handler_{};

  // --------------

  struct Video {
    VideoDecoder decoder{};

    VkSamplerYcbcrConversionInfo ycbcr_info{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
      .pNext = nullptr,
    };
    VkSamplerYcbcrConversion ycbcr_conversion{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};

    struct Frame {
      AImage* android_image{};

      VkImage image{VK_NULL_HANDLE};
      VkDeviceMemory memory{VK_NULL_HANDLE};
      VkImageView view{VK_NULL_HANDLE};
    } frame{};

    PFN_vkGetAndroidHardwareBufferPropertiesANDROID getAndroidHardwareBufferPropertiesANDROID{};
  } video_;

  // --------------

  backend::Buffer uniform_buffer_{};
  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{};

  VkPipelineLayout pipeline_layout_{};
  Pipeline graphics_pipeline_{};
  shader_interop::PushConstant push_constant_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(VideoPlayerSample)

/* -------------------------------------------------------------------------- */
