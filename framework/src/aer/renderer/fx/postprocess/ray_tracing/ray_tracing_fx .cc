#include "aer/renderer/fx/postprocess/ray_tracing/ray_tracing_fx.h"

#include "aer/platform/backend/accel_struct.h"
#include "aer/scene/vertex_internal.h" // (for material_shader_interop)

/* -------------------------------------------------------------------------- */

void RayTracingFx::execute(CommandEncoder& cmd) const {
  cmd.bind_pipeline(pipeline_);

  // Bind descriptor sets.
  {
    auto const& DSR = context_ptr_->descriptor_set_registry();

    VkShaderStageFlags const stage_flags{
        VK_SHADER_STAGE_RAYGEN_BIT_KHR
      | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
      | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
    };

    cmd.bind_descriptor_set(
      descriptor_set_,
      pipeline_layout_,
      stage_flags,
      material_shader_interop::kDescriptorSet_Internal
    );

    cmd.bind_descriptor_set(
      DSR.descriptor(DescriptorSetRegistry::Type::Frame).set,
      pipeline_layout_,
      stage_flags,
      material_shader_interop::kDescriptorSet_Frame
    );

    cmd.bind_descriptor_set(
      DSR.descriptor(DescriptorSetRegistry::Type::Scene).set,
      pipeline_layout_,
      stage_flags,
      material_shader_interop::kDescriptorSet_Scene
    );

    cmd.bind_descriptor_set(
      DSR.descriptor(DescriptorSetRegistry::Type::RayTracing).set,
      pipeline_layout_,
      stage_flags,
      material_shader_interop::kDescriptorSet_RayTracing
    );
  }

  pushConstant(cmd);

  cmd.pipeline_image_barriers(barriers_.images_start);
  // cmd.pipeline_image_barriers(barriers_.buffers_start);

  cmd.trace_rays(region_, dimension_.width, dimension_.height);

  // cmd.pipeline_image_barriers(barriers_.buffers_end);
  cmd.pipeline_image_barriers(barriers_.images_end);
}

// ----------------------------------------------------------------------------

bool RayTracingFx::resize(VkExtent2D const dimension) {
  LOG_CHECK((dimension.width > 0) && (dimension.height > 0));

  bool const has_resized = (dimension.width != dimension_.width)
                        || (dimension.height != dimension_.height);
  bool const has_outputs = !out_images_.empty() || !out_images_.empty();

  if (!has_resized && has_outputs) {
    return false;
  }

  dimension_ = dimension;

  releaseOutputImagesAndBuffers(); //
  // --------------------------------------
  out_images_ = {
    context_ptr_->create_image_2d(
      dimension_.width, dimension_.height,
      VK_FORMAT_R16G16B16A16_SFLOAT, // (for accumulation)
        VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_STORAGE_BIT
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT // (for blitting)
      ,
      "RayTracingFx::AccumulationImage"
    )
  };
  // --------------------------------------
  resetMemoryBarriers();

  return true;
}

// ----------------------------------------------------------------------------

void RayTracingFx::resetMemoryBarriers() {
  if (!out_images_.empty()) {
    barriers_.images_start.resize(out_images_.size(), {
      .srcStageMask  = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
      .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                     | VK_ACCESS_2_SHADER_WRITE_BIT
                     ,
      .dstStageMask  = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT
                     | VK_ACCESS_2_SHADER_WRITE_BIT
                     ,
      .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED, //VK_IMAGE_LAYOUT_GENERAL,
      .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .subresourceRange = {
          .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel   = 0,
          .levelCount     = 1,
          .baseArrayLayer = 0,
          .layerCount     = 1,
      }
    });

    barriers_.images_end.resize(out_images_.size(), {
      .srcStageMask  = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
      .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT
                     | VK_ACCESS_2_SHADER_READ_BIT
                     ,
      .dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                     | VK_PIPELINE_STAGE_2_TRANSFER_BIT
                     ,
      .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                     | VK_ACCESS_2_TRANSFER_READ_BIT
                     ,
      .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .subresourceRange = {
          .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel   = 0,
          .levelCount     = 1,
          .baseArrayLayer = 0,
          .layerCount     = 1,
      }
    });

    for (size_t i = 0u; i < out_images_.size(); ++i) {
      auto const& img = out_images_[i].image;
      barriers_.images_start[i].image = img;
      barriers_.images_end[i].image   = img;
    }
  }
}

// ----------------------------------------------------------------------------

void RayTracingFx::createPipeline() {
  auto shaders_map = createShaderModules();

  auto pipeline_desc = pipelineDescriptor(shaders_map);

  pipeline_ = context_ptr_->create_raytracing_pipeline(
    pipeline_layout_,
    pipeline_desc
  );

  for (auto const& [_, shaders] : shaders_map) {
    for (auto const& shader : shaders) {
      context_ptr_->release_shader_module(shader);
    }
  }

  // The default SBT holds all groups requested.
  // [In the future we might prefers build them dynamically].
  buildShaderBindingTable(pipeline_desc);
}

// ----------------------------------------------------------------------------

void RayTracingFx::buildShaderBindingTable(RayTracingPipelineDescriptor_t const& desc) {
  VkPhysicalDeviceRayTracingPipelinePropertiesKHR rtProps{
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
  };
  VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
  props2.pNext = &rtProps;
  vkGetPhysicalDeviceProperties2(context_ptr_->physical_device(), &props2);
  uint32_t const handleSize         = rtProps.shaderGroupHandleSize;
  uint32_t const handleAlignment    = rtProps.shaderGroupHandleAlignment;
  uint32_t const handleSizeAligned  = utils::AlignTo(handleSize, handleAlignment);
  uint32_t const baseAlignment      = rtProps.shaderGroupBaseAlignment;

  auto const& sg = desc.shaderGroups;
  uint32_t const numRayGen    = static_cast<uint32_t>(sg.raygens.size());
  uint32_t const numMiss      = static_cast<uint32_t>(sg.misses.size());
  uint32_t const numHit       = static_cast<uint32_t>(sg.hits.size());
  uint32_t const numCallable  = static_cast<uint32_t>(sg.callables.size());
  uint32_t const numGroups    = numRayGen + numMiss + numHit + numCallable;

  std::vector<std::byte> shader_handles(numGroups * handleSize);
  CHECK_VK(vkGetRayTracingShaderGroupHandlesKHR(
    context_ptr_->device(),
    pipeline_.handle(),
    0,
    numGroups,
    shader_handles.size(),
    shader_handles.data()
  ));


  // ShadersGroups.

  size_t const offsetRayGen   = 0;
  size_t const sizeRayGen     = numRayGen * handleSizeAligned;

  size_t const offsetMiss     = utils::AlignTo(offsetRayGen + sizeRayGen, baseAlignment);
  size_t const sizeMiss       = numMiss * handleSizeAligned;

  size_t const offsetHit      = utils::AlignTo(offsetMiss + sizeMiss, baseAlignment);
  size_t const sizeHit        = numHit * handleSizeAligned;

  size_t const offsetCallable = utils::AlignTo(offsetHit + sizeHit, baseAlignment);
  size_t const sizeCallable   = numCallable * handleSizeAligned;


  size_t const sbt_buffersize = offsetCallable + sizeCallable;

  sbt_storage_buffer_ = allocator_ptr_->create_buffer(
    sbt_buffersize,
      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    | VK_BUFFER_USAGE_TRANSFER_SRC_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ,
    VMA_MEMORY_USAGE_CPU_TO_GPU
  );

  backend::Buffer staging_buffer = allocator_ptr_->create_staging_buffer(sbt_buffersize);

  // Map staging and fill regions with shader handles
  {
    void* mapped;
    allocator_ptr_->map_memory(staging_buffer, &mapped);

    uint8_t* pData = reinterpret_cast<uint8_t*>(mapped);

    auto copyHandles = [&](uint32_t firstGroup, uint32_t count, size_t dstOffset) {
      for (uint32_t i = 0; i < count; i++) {
        std::byte* dst = reinterpret_cast<std::byte*>(pData + dstOffset + i * handleSizeAligned);

        // Copy shadergroup identifier.
        std::byte* src = shader_handles.data() + (firstGroup + i) * handleSize;
        std::memcpy(dst, src, handleSize);

        // If we allocated enough memory we could copy extra data here
        // to be retrieved in the shaders.
      }
    };

    uint32_t groupOffset = 0;
    copyHandles(groupOffset, numRayGen, offsetRayGen);
    groupOffset += numRayGen;

    copyHandles(groupOffset, numMiss, offsetMiss);
    groupOffset += numMiss;

    copyHandles(groupOffset, numHit, offsetHit);
    groupOffset += numHit;

    copyHandles(groupOffset, numCallable, offsetCallable);

    allocator_ptr_->unmap_memory(staging_buffer);
  }

  context_ptr_->copy_buffer(
    staging_buffer, sbt_storage_buffer_, sbt_buffersize
  );
  context_ptr_->device_wait_idle();

  auto getRegion = [&](size_t offset, size_t size) -> VkStridedDeviceAddressRegionKHR {
    VkBufferDeviceAddressInfo addrInfo{
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = sbt_storage_buffer_.buffer,
    };

    VkDeviceAddress baseAddress = vkGetBufferDeviceAddressKHR(
      context_ptr_->device(), &addrInfo
    );
    return {
      .deviceAddress = baseAddress + offset,
      .stride = handleSizeAligned,
      .size = size,
    };
  };

  region_.raygen   = getRegion(  offsetRayGen, sizeRayGen);
  region_.miss     = getRegion(    offsetMiss, sizeMiss);
  region_.hit      = getRegion(     offsetHit, sizeHit);
  region_.callable = getRegion(offsetCallable, sizeCallable);
}

/* -------------------------------------------------------------------------- */