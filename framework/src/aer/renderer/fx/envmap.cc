/* -------------------------------------------------------------------------- */

#include "aer/renderer/fx/envmap.h"
#include "aer/renderer/renderer.h"

/* -------------------------------------------------------------------------- */

void Envmap::init(RenderContext const& context) {
  auto const& allocator = context.allocator();

  context_ptr_ = &context;

  irradiance_matrices_buffer_ = allocator.create_buffer(
    sizeof(shader_interop::envmap::SHMatrices),
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
    | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR
  );

  /* Create the HDR envmaps & the BRDF LUT. */
  {
    VkImageCreateInfo image_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
      .imageType = VK_IMAGE_TYPE_2D,
      .mipLevels = 1u,
      .arrayLayers = 6u,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_STORAGE_BIT
             | VK_IMAGE_USAGE_SAMPLED_BIT
             ,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkImageViewCreateInfo view_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
      .components = {
        VK_COMPONENT_SWIZZLE_R,
        VK_COMPONENT_SWIZZLE_G,
        VK_COMPONENT_SWIZZLE_B,
        VK_COMPONENT_SWIZZLE_A,
      },
      .subresourceRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0u,
        .levelCount = image_info.mipLevels,
        .baseArrayLayer = 0u,
        .layerCount = image_info.arrayLayers,
      },
    };

    image_info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    view_info.format = image_info.format;

    image_info.extent = { kDiffuseResolution, kDiffuseResolution, 1u };
    allocator.create_image_with_view(image_info, view_info, &images_[ImageType::Diffuse]);

    image_info.extent = { kIrradianceResolution, kIrradianceResolution, 1u };
    allocator.create_image_with_view(image_info, view_info, &images_[ImageType::Irradiance]);

    image_info.extent = { kSpecularResolution, kSpecularResolution, 1u };
    image_info.mipLevels = kSpecularLevelCount;
    // view_info.subresourceRange.baseMipLevel = 2u;
    view_info.subresourceRange.levelCount = image_info.mipLevels;
    allocator.create_image_with_view(image_info, view_info, &images_[ImageType::Specular]);
  }

  /* Shared descriptor sets */
  {
    VkDescriptorBindingFlags const kDefaultDescBindingFlags{
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
      | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
    };

    descriptor_set_layout_ = context_ptr_->create_descriptor_set_layout({
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_Sampler,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = kDefaultDescBindingFlags,
      },
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_StorageImage,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = kDefaultDescBindingFlags,
      },
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_StorageImageArray,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = kSpecularLevelCount,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = kDefaultDescBindingFlags,
      },
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_IrradianceSHCoeff_StorageBuffer,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = kDefaultDescBindingFlags,
      },
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_IrradianceSHMatrices_StorageBuffer,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = kDefaultDescBindingFlags,
      },
    });

    descriptor_set_ = context_ptr_->create_descriptor_set(descriptor_set_layout_, {
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_StorageImage,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .images = {
          {
            .imageView = images_[ImageType::Diffuse].view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          }
        }
      },
      {
        .binding = shader_interop::envmap::kDescriptorSetBinding_IrradianceSHMatrices_StorageBuffer,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffers = { { irradiance_matrices_buffer_.buffer } }
      },
    });
  }

  pipeline_layout_ = context_ptr_->create_pipeline_layout({
    .setLayouts = { descriptor_set_layout_ },
    .pushConstantRanges = {
      {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
                    ,
        .size = sizeof(push_constant_),
      }
    },
  });

  /* Create the compute pipelines. */
  {
    auto shaders{context_ptr_->create_shader_modules(FRAMEWORK_COMPILED_SHADERS_DIR "envmap", {
      "spherical_to_cubemap.comp.glsl",
      "irradiance_calculate_coeff.comp.glsl",
      "irradiance_reduce_step.comp.glsl",
      "irradiance_transfer_coeff.comp.glsl",
      "irradiance_convolution.comp.glsl",
      "specular_convolution.comp.glsl",
    })};
    context_ptr_->create_compute_pipelines(pipeline_layout_, shaders, compute_pipelines_.data());
    context_ptr_->release_shader_modules(shaders);
  }

  /* internal sampler */
  {
    VkSamplerCreateInfo const sampler_create_info{
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = VK_FILTER_LINEAR,
      .minFilter = VK_FILTER_LINEAR,
      // .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
      .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, //
      .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, //
      .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      .anisotropyEnable = VK_FALSE,
      .maxLod = 0,
    };
    CHECK_VK( vkCreateSampler(context_ptr_->device(), &sampler_create_info, nullptr, &sampler_) );
  }
}

// ----------------------------------------------------------------------------

void Envmap::release() {
  if (!context_ptr_) {
    return;
  }

  context_ptr_->allocator().destroy_buffer(irradiance_matrices_buffer_);
  vkDestroySampler(context_ptr_->device(), sampler_, nullptr); //
  for (auto &image : images_) {
    context_ptr_->allocator().destroy_image(&image);
  }
  for (auto pipeline : compute_pipelines_) {
    context_ptr_->destroy_pipeline(pipeline);
  }
  context_ptr_->destroy_pipeline_layout(pipeline_layout_);
  context_ptr_->destroy_descriptor_set_layout(descriptor_set_layout_);
}

// ----------------------------------------------------------------------------

bool Envmap::setup(std::string_view hdr_filename) {
  if (!load_diffuse_envmap(hdr_filename)) {
    LOGE("Fail to load spherical map \"{}\".", hdr_filename);
    return false;
  }

  context_ptr_->update_descriptor_set(descriptor_set_, {
    {
      .binding = shader_interop::envmap::kDescriptorSetBinding_Sampler,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = {
        {
          .sampler = sampler_, //
          .imageView = get_image(ImageType::Diffuse).view,
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }
      }
    }
  });

  compute_irradiance_sh_coeff();
  compute_irradiance();
  compute_specular();

  return true;
}

// ----------------------------------------------------------------------------

bool Envmap::load_diffuse_envmap(std::string_view hdr_filename) {
  backend::Image spherical_envmap{};
  if (!context_ptr_->load_image_2d(hdr_filename, spherical_envmap)) {
    return false;
  }

  context_ptr_->update_descriptor_set(descriptor_set_, {
    {
      .binding = shader_interop::envmap::kDescriptorSetBinding_Sampler,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = {
        {
          .sampler = sampler_,
          .imageView = spherical_envmap.view,
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }
      }
    }
  });

  auto const& diffuse = images_[ImageType::Diffuse];

  /* Transform the spherical texture into a cubemap. */
  auto cmd = context_ptr_->create_transient_command_encoder(Context::TargetQueue::Compute);
  {
    cmd.pipeline_image_barriers({
      {
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, //
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = diffuse.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
      }
    });

    cmd.bind_pipeline(compute_pipelines_[ComputeStage::TransformSpherical]);
    {
      cmd.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_COMPUTE_BIT);

      push_constant_.mapResolution = kDiffuseResolution; //
      cmd.push_constant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch<
        shader_interop::envmap::kCompute_SphericalTransform_kernelSize_x,
        shader_interop::envmap::kCompute_SphericalTransform_kernelSize_y
      >(push_constant_.mapResolution, push_constant_.mapResolution, 6u);
    }

    cmd.pipeline_image_barriers({
      {
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = diffuse.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
      }
    });
  }
  context_ptr_->finish_transient_command_encoder(cmd);

  context_ptr_->allocator().destroy_image(&spherical_envmap);

  return true;
}

// ----------------------------------------------------------------------------

void Envmap::compute_irradiance_sh_coeff() {
  uint32_t const faceResolution = kDiffuseResolution * kDiffuseResolution;
  uint32_t const reduceKernelSize = shader_interop::envmap::kCompute_IrradianceReduceSHCoeff_kernelSize_x;

  /* Allocate a buffer large enough to ping pong input/output of the reduce stages. */
  uint32_t const bufferSize = faceResolution
                            + vk_utils::GetKernelGridDim(faceResolution, reduceKernelSize)
                            ;

  auto const& diffuse = images_[ImageType::Diffuse];

  backend::Buffer sh_coefficient_buffer{context_ptr_->allocator().create_buffer(
    bufferSize * sizeof(shader_interop::envmap::SHCoeff),
      VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR
  )};

  context_ptr_->update_descriptor_set(descriptor_set_, {
    {
      .binding = shader_interop::envmap::kDescriptorSetBinding_Sampler,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = {
        {
          .sampler = sampler_, //
          .imageView = diffuse.view,
          .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }
      }
    },
    {
      .binding = shader_interop::envmap::kDescriptorSetBinding_IrradianceSHCoeff_StorageBuffer,
      .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .buffers = { { sh_coefficient_buffer.buffer } }
    }
  });

  // --------------------

  auto cmd = context_ptr_->create_transient_command_encoder(Context::TargetQueue::Compute);
  {
    cmd.bind_descriptor_set(descriptor_set_, pipeline_layout_, VK_SHADER_STAGE_COMPUTE_BIT);

    /* Compute Coefficient for each pixels of the cubemap faces. */
    cmd.bind_pipeline(compute_pipelines_[ComputeStage::IrradianceSHCoeff]);
    {
      push_constant_.mapResolution = kDiffuseResolution;
      cmd.push_constant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);
      cmd.dispatch<
        shader_interop::envmap::kCompute_IrradianceSHCoeff_kernelSize_x,
        shader_interop::envmap::kCompute_IrradianceSHCoeff_kernelSize_y
      >(kDiffuseResolution, kDiffuseResolution);
    }

    /* Reduce the Spherical Harmonics coefficients buffer. */
    cmd.bind_pipeline(compute_pipelines_[ComputeStage::ReduceSHCoeff]);
    uint32_t nelems = faceResolution;
    uint32_t buffer_binding = 0u;
    while (nelems > 1u) {
      uint32_t const ngroups{vk_utils::GetKernelGridDim(nelems, reduceKernelSize)};

      uint64_t const read_buffer_bytesize{nelems * sizeof(shader_interop::envmap::SHCoeff)};
      uint64_t const write_buffer_bytesize{ngroups * sizeof(shader_interop::envmap::SHCoeff)};

      uint32_t const read_offset{ faceResolution * buffer_binding };
      uint32_t const write_offset{ faceResolution * (buffer_binding ^ 1u) };

      cmd.pipeline_buffer_barriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .buffer = sh_coefficient_buffer.buffer,
          .offset = read_offset * sizeof(shader_interop::envmap::SHCoeff), //
          .size = read_buffer_bytesize, //
        },
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
          .buffer = sh_coefficient_buffer.buffer,
          .offset = write_offset * sizeof(shader_interop::envmap::SHCoeff), //
          .size = write_buffer_bytesize, //
        },
      });

      push_constant_.numElements = nelems;
      push_constant_.readOffset = read_offset;
      push_constant_.writeOffset = write_offset;
      cmd.push_constant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch<reduceKernelSize>(nelems);

      nelems = ngroups;
      buffer_binding ^= 1u;
    }

    cmd.pipeline_buffer_barriers({
      {
        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = sh_coefficient_buffer.buffer,
        .offset = push_constant_.writeOffset,
        .size = sizeof(shader_interop::envmap::SHCoeff)
      }
    });

    /* Transfer and transform the reduced SHCoeffs as irradiance matrices. */
    cmd.bind_pipeline(compute_pipelines_[ComputeStage::IrradianceTransfer]);
    {
      cmd.dispatch();

      cmd.pipeline_buffer_barriers({
        {
          .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
                        | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
                        ,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
          .buffer = irradiance_matrices_buffer_.buffer,
        }
      });
    }
  }
  context_ptr_->finish_transient_command_encoder(cmd);

  context_ptr_->allocator().destroy_buffer(sh_coefficient_buffer);
}

// ----------------------------------------------------------------------------

void Envmap::compute_irradiance() {
  auto const& irradiance = images_[ImageType::Irradiance];

  context_ptr_->update_descriptor_set(descriptor_set_, {
    {
      .binding = shader_interop::envmap::kDescriptorSetBinding_StorageImage,
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .images = {
        {
          .imageView = irradiance.view,
          .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
        }
      }
    }
  });

  auto cmd = context_ptr_->create_transient_command_encoder(Context::TargetQueue::Compute);
  {
    cmd.pipeline_image_barriers({
      {
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = irradiance.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
      }
    });

    cmd.bind_pipeline(compute_pipelines_[ComputeStage::Irradiance]);
    {
      cmd.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_COMPUTE_BIT);

      push_constant_.mapResolution = kIrradianceResolution;
      cmd.push_constant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch<
        shader_interop::envmap::kCompute_Irradiance_kernelSize_x,
        shader_interop::envmap::kCompute_Irradiance_kernelSize_y
      >(push_constant_.mapResolution, push_constant_.mapResolution, 6u);
    }

    cmd.pipeline_image_barriers({
      {
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = irradiance.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 }
      }
    });
  }
  context_ptr_->finish_transient_command_encoder(cmd);
}

// ----------------------------------------------------------------------------

void Envmap::compute_specular() {
  auto const& specular = images_[ImageType::Specular];

  /* Create an imageView for each mip level to render into. */
  VkImageViewCreateInfo view_info{
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = specular.image,
    .viewType = VK_IMAGE_VIEW_TYPE_CUBE,
    .format = VK_FORMAT_R16G16B16A16_SFLOAT,
    .components = {
      VK_COMPONENT_SWIZZLE_R,
      VK_COMPONENT_SWIZZLE_G,
      VK_COMPONENT_SWIZZLE_B,
      VK_COMPONENT_SWIZZLE_A,
    },
    .subresourceRange = {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0u,
      .levelCount = 1,
      .baseArrayLayer = 0u,
      .layerCount = kFaceCount,
    },
  };
  std::vector<VkDescriptorImageInfo> desc_image_infos(kSpecularLevelCount, {
    .sampler = VK_NULL_HANDLE,
    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
  });

  for (uint32_t level = 0u; level < kSpecularLevelCount; ++level) {
    view_info.subresourceRange.baseMipLevel = level;
    CHECK_VK(vkCreateImageView(
      context_ptr_->device(), &view_info, nullptr, &desc_image_infos[level].imageView
    ));
  }

  context_ptr_->update_descriptor_set(descriptor_set_, {
    {
      .binding = shader_interop::envmap::kDescriptorSetBinding_StorageImageArray,
      .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .images = desc_image_infos,
    }
  });

  auto cmd = context_ptr_->create_transient_command_encoder(Context::TargetQueue::Compute);
  {
    cmd.pipeline_image_barriers({
      {
          .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
          .srcAccessMask = 0,
          .dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = specular.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, kSpecularLevelCount, 0, kFaceCount }
      }
    });

    cmd.bind_pipeline(compute_pipelines_[ComputeStage::Specular]);
    cmd.bind_descriptor_set(descriptor_set_, VK_SHADER_STAGE_COMPUTE_BIT);
    for (uint32_t level = 0u; level < kSpecularLevelCount; ++level) {
      float const roughness = static_cast<float>(level) * kInvMaxSpecularLevel;

      push_constant_.mapResolution = kSpecularResolution >> level;
      push_constant_.numSamples = kSpecularSampleCount;
      push_constant_.roughnessSquared = std::pow(roughness, 2.0f);
      push_constant_.mipLevel = level;
      cmd.push_constant(push_constant_, VK_SHADER_STAGE_COMPUTE_BIT);

      cmd.dispatch<
        shader_interop::envmap::kCompute_Specular_kernelSize_x,
        shader_interop::envmap::kCompute_Specular_kernelSize_y,
        1u
      >(push_constant_.mapResolution, push_constant_.mapResolution, kFaceCount);
    }

    cmd.pipeline_image_barriers({
      {
          .srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
          .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
          .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .image = specular.image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, kSpecularLevelCount, 0, kFaceCount }
      }
    });
  }
  context_ptr_->finish_transient_command_encoder(cmd);

  for (auto const& desc_image_info : desc_image_infos) {
    vkDestroyImageView(context_ptr_->device(), desc_image_info.imageView, nullptr);
  }
}

/* -------------------------------------------------------------------------- */
