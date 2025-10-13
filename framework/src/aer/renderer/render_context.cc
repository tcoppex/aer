#include "aer/renderer/render_context.h"

#include "aer/platform/swapchain_interface.h" //
#include "aer/scene/image_data.h" // ~
#include "aer/shaders/material/interop.h" // for kAttribLocation_*


/* -------------------------------------------------------------------------- */

namespace {

char const* kDefaulShaderEntryPoint{
  "main"
};

}

/* -------------------------------------------------------------------------- */

bool RenderContext::init(
  std::string_view app_name,
  std::vector<char const*> const& instance_extensions,
  std::shared_ptr<XRVulkanInterface> vulkan_xr
) {
  if (!Context::init(app_name, instance_extensions, vulkan_xr)) {
    return false;
  }

  LOGD("-- RenderContext --");

  /* Create the shared pipeline cache. */
  LOGD(" > PipelineCacheInfo");
  {
    VkPipelineCacheCreateInfo const cache_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
      .initialDataSize = 0u,
      .pInitialData = nullptr,
    };
    CHECK_VK(vkCreatePipelineCache(
      device(),
      &cache_info,
      nullptr,
      &pipeline_cache_
    ));
  }

  // Handle the app samplers.
  sampler_pool_.init(device());

  // Handle Descriptor Set allocation through the framework.
  LOGD(" > Descriptor Registry");
  descriptor_set_registry_.init(*this, kMaxDescriptorPoolSets);

  return true;
}

// ----------------------------------------------------------------------------

void RenderContext::deinit() {
  if (device() == VK_NULL_HANDLE) {
    return;
  }

  sampler_pool_.deinit();
  descriptor_set_registry_.release();
  vkDestroyPipelineCache(device(), pipeline_cache_, nullptr);

  Context::deinit();
}

// ----------------------------------------------------------------------------

std::unique_ptr<RenderTarget> RenderContext::create_render_target() const {
  return std::unique_ptr<RenderTarget>(new RenderTarget(*this));
}

// ----------------------------------------------------------------------------

std::unique_ptr<RenderTarget> RenderContext::create_render_target(
  RenderTarget::Descriptor const& desc
) const {
  if (auto rt = create_render_target(); rt) {
    rt->setup(desc);
    return rt;
  }
  return nullptr;
}

// ----------------------------------------------------------------------------

std::unique_ptr<Framebuffer> RenderContext::create_framebuffer(
  SwapchainInterface const& swapchain
) const {
  return std::unique_ptr<Framebuffer>(new Framebuffer(*this, swapchain));
}

// ----------------------------------------------------------------------------

std::unique_ptr<Framebuffer> RenderContext::create_framebuffer(
  SwapchainInterface const& swapchain,
  Framebuffer::Descriptor_t const& desc
) const {
  if (auto framebuffer = create_framebuffer(swapchain); framebuffer) {
    framebuffer->setup(desc);
    return framebuffer;
  }
  return nullptr;
}

// ----------------------------------------------------------------------------

void RenderContext::destroy_pipeline_layout(VkPipelineLayout layout) const {
  vkDestroyPipelineLayout(device(), layout, nullptr);
}

// ----------------------------------------------------------------------------

VkPipelineLayout RenderContext::create_pipeline_layout(
  PipelineLayoutDescriptor_t const& params
) const {
  for (size_t i = 1u; i < params.pushConstantRanges.size(); ++i) {
    if (params.pushConstantRanges[i].offset == 0u) {
      LOGW("[Warning] 'create_pipeline_layout' has constant ranges with no offsets.");
      break;
    }
  }

  VkPipelineLayoutCreateInfo const pipeline_layout_create_info{
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = static_cast<uint32_t>(params.setLayouts.size()),
    .pSetLayouts = params.setLayouts.data(),
    .pushConstantRangeCount = static_cast<uint32_t>(params.pushConstantRanges.size()),
    .pPushConstantRanges = params.pushConstantRanges.data(),
  };
  VkPipelineLayout pipeline_layout;
  CHECK_VK(vkCreatePipelineLayout(
    device(),
    &pipeline_layout_create_info,
    nullptr,
    &pipeline_layout
  ));
  return pipeline_layout;
}

// ----------------------------------------------------------------------------

void RenderContext::create_graphics_pipelines(
  VkPipelineLayout pipeline_layout,
  std::vector<VkGraphicsPipelineCreateInfo> const& _create_infos,
  std::vector<Pipeline> *out_pipelines
) const {
  LOG_CHECK( out_pipelines != nullptr && !out_pipelines->empty() );
  LOG_CHECK( pipeline_layout != VK_NULL_HANDLE );
  LOG_CHECK( !_create_infos.empty() );

  /// When batching pipelines, most underlying data will not changes, so
  /// we could improve setupping by changing only those needed (like
  /// color_blend_attachments).
  std::vector<GraphicsPipelineCreateInfoData_t> datas(_create_infos.size());

  std::vector<VkGraphicsPipelineCreateInfo> create_infos = _create_infos;
  for (size_t i = 0; i < _create_infos.size(); ++i) {
    create_infos[i].flags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    create_infos[i].basePipelineIndex = 0;
  }
  if (!_create_infos.empty()) {
    create_infos[0].flags &= ~VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    create_infos[0].flags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    create_infos[0].basePipelineIndex = -1;
  }

  std::vector<VkPipeline> pipelines(_create_infos.size());
  CHECK_VK(vkCreateGraphicsPipelines(
    device(),
    pipeline_cache_,
    create_infos.size(),
    create_infos.data(),
    nullptr,
    pipelines.data()
  ));

  for (size_t i = 0; i < _create_infos.size(); ++i) {
    (*out_pipelines)[i] = Pipeline(
      pipeline_layout,
      pipelines[i],
      VK_PIPELINE_BIND_POINT_GRAPHICS
    );
    vkutils::SetDebugObjectName(device(), pipelines[i], "GraphicsPipeline::NoName");
  }
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_graphics_pipeline(
  VkPipelineLayout pipeline_layout,
  VkGraphicsPipelineCreateInfo const& create_info
) const {
  LOG_CHECK( pipeline_layout != VK_NULL_HANDLE );

  VkPipeline pipeline{};
  CHECK_VK(vkCreateGraphicsPipelines(
    device(), pipeline_cache_, 1u, &create_info, nullptr, &pipeline
  ));

  return Pipeline(pipeline_layout, pipeline, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_graphics_pipeline(
  PipelineLayoutDescriptor_t const& layout_desc,
  VkGraphicsPipelineCreateInfo const& create_info
) const {
  Pipeline pipeline = create_graphics_pipeline(
    create_pipeline_layout(layout_desc),
    create_info
  );
  pipeline.use_internal_layout_ = true;
  return pipeline;
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_graphics_pipeline(
  VkGraphicsPipelineCreateInfo const& create_info
) const {
  return create_graphics_pipeline(PipelineLayoutDescriptor_t(), create_info);
}

// ----------------------------------------------------------------------------

void RenderContext::create_compute_pipelines(
  VkPipelineLayout pipeline_layout,
  std::vector<backend::ShaderModule> const& modules,
  Pipeline *pipelines
) const {
  LOG_CHECK(pipelines != nullptr);

  std::vector<VkComputePipelineCreateInfo> pipeline_infos(modules.size(), {
    .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
    .stage = {
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = VK_NULL_HANDLE,
      .pName  = kDefaulShaderEntryPoint,
    },
    .layout = pipeline_layout,
  });
  for (size_t i = 0; i < modules.size(); ++i) {
    pipeline_infos[i].stage.module = modules[i].module;
  }
  std::vector<VkPipeline> pips(modules.size());

  CHECK_VK(vkCreateComputePipelines(
    device(),
    pipeline_cache_,
    static_cast<uint32_t>(pipeline_infos.size()),
    pipeline_infos.data(),
    nullptr,
    pips.data()
  ));

  for (size_t i = 0; i < pips.size(); ++i) {
    pipelines[i] = Pipeline(pipeline_layout, pips[i], VK_PIPELINE_BIND_POINT_COMPUTE);
  }
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_compute_pipeline(
  VkPipelineLayout pipeline_layout,
  backend::ShaderModule const& module
) const {
  Pipeline p;
  create_compute_pipelines(pipeline_layout, { module }, &p);
  return p;
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_raytracing_pipeline(
  VkPipelineLayout pipeline_layout,
  RayTracingPipelineDescriptor_t const& desc
) const {
  std::vector<VkPipelineShaderStageCreateInfo> stage_infos{};

  // Shaders.
  {
    auto const& s = desc.shaders;

    stage_infos.reserve(
      s.raygens.size() + s.misses.size()        + s.closestHits.size() +
      s.anyHits.size() + s.intersections.size() + s.callables.size()
    );

    auto entry_point{[](auto const& stage) {
      return kDefaulShaderEntryPoint;
      // return stage.entryPoint.empty() ? kDefaulShaderEntryPoint
      //                                 : stage.entryPoint.c_str()
      //                                 ;
    }};

    auto insert_shaders{[&](auto const& stages, VkShaderStageFlagBits flag) {
      for (auto const& stage : stages) {
        stage_infos.push_back({
          .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage  = flag,
          .module = stage.module,
          .pName  = entry_point(stage),
        });
      }
    }};

    insert_shaders(s.raygens,        VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    insert_shaders(s.anyHits,        VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
    insert_shaders(s.closestHits,    VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    insert_shaders(s.misses,         VK_SHADER_STAGE_MISS_BIT_KHR);
    insert_shaders(s.intersections,  VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
    insert_shaders(s.callables,      VK_SHADER_STAGE_CALLABLE_BIT_KHR);
  }

  // ShaderGroups.
  std::vector<VkRayTracingShaderGroupCreateInfoKHR> shaderGroups{};
  {
    auto const& sg = desc.shaderGroups;

    shaderGroups.resize(
      sg.raygens.size() + sg.misses.size() + sg.hits.size() + sg.callables.size(),
      {
        .sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR,
        .generalShader      = VK_SHADER_UNUSED_KHR,
        .closestHitShader   = VK_SHADER_UNUSED_KHR,
        .anyHitShader       = VK_SHADER_UNUSED_KHR,
        .intersectionShader = VK_SHADER_UNUSED_KHR
      }
    );
    size_t sg_index{0};
    for (auto const& raygengroup : sg.raygens) {
      LOG_CHECK((raygengroup.type == 0)
             || (raygengroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
      );
      shaderGroups[sg_index].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroups[sg_index].generalShader = raygengroup.generalShader;
      sg_index++;
    }
    for (auto const& missgroup : sg.misses) {
      LOG_CHECK((missgroup.type == 0)
             || (missgroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
      );
      shaderGroups[sg_index].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroups[sg_index].generalShader = missgroup.generalShader;
      sg_index++;
    }
    for (auto const& hitgroup : sg.hits) {
      LOG_CHECK((hitgroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR)
             || (hitgroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR)
      );
      shaderGroups[sg_index].type               = hitgroup.type;
      shaderGroups[sg_index].closestHitShader   = hitgroup.closestHitShader;
      shaderGroups[sg_index].anyHitShader       = hitgroup.anyHitShader;
      shaderGroups[sg_index].intersectionShader = hitgroup.intersectionShader;
      sg_index++;
    }
    for (auto const& callgroup : sg.callables) {
      LOG_CHECK((callgroup.type == 0)
             || (callgroup.type == VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR)
      );
      shaderGroups[sg_index].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
      shaderGroups[sg_index].generalShader = callgroup.generalShader;
      sg_index++;
    }
  }

  VkRayTracingPipelineCreateInfoKHR const raytracing_pipeline_create_info{
    .sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR,
    .flags = 0,
    .stageCount = static_cast<uint32_t>(stage_infos.size()),
    .pStages = stage_infos.data(),
    .groupCount = static_cast<uint32_t>(shaderGroups.size()),
    .pGroups = shaderGroups.data(),
    .maxPipelineRayRecursionDepth = desc.maxPipelineRayRecursionDepth,
    .pLibraryInfo = nullptr,
    .pLibraryInterface = nullptr,
    .pDynamicState = nullptr,
    .layout = pipeline_layout,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1,
  };

  VkDeferredOperationKHR const deferredOperation{ VK_NULL_HANDLE }; // (unused)

  VkPipeline pipeline;
  CHECK_VK(vkCreateRayTracingPipelinesKHR(
    device(),
    deferredOperation,
    pipeline_cache_,
    1,
    &raytracing_pipeline_create_info,
    nullptr,
    &pipeline
  ));

  return Pipeline(
    pipeline_layout,
    pipeline,
    VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR
  );
}

// ----------------------------------------------------------------------------

void RenderContext::destroy_pipeline(Pipeline const& pipeline) const {
  vkDestroyPipeline(device(), pipeline.handle(), nullptr);
  if (pipeline.use_internal_layout_) {
    destroy_pipeline_layout(pipeline.layout());
  }
}

// ----------------------------------------------------------------------------

VkDescriptorSetLayout RenderContext::create_descriptor_set_layout(
  DescriptorSetLayoutParamsBuffer const& params,
  VkDescriptorSetLayoutCreateFlags flags
) const {
  return descriptor_set_registry_.create_layout(params, flags);
}

// ----------------------------------------------------------------------------

void RenderContext::destroy_descriptor_set_layout(
  VkDescriptorSetLayout &layout
) const {
  descriptor_set_registry_.destroy_layout(layout);
}

// ----------------------------------------------------------------------------

VkDescriptorSet RenderContext::create_descriptor_set(
  VkDescriptorSetLayout const layout
) const {
  return descriptor_set_registry_.allocate_descriptor_set(layout);
}

// ----------------------------------------------------------------------------

VkDescriptorSet RenderContext::create_descriptor_set(
  VkDescriptorSetLayout const layout,
  std::vector<DescriptorSetWriteEntry> const& entries
) const {
  auto const descriptor_set{ create_descriptor_set(layout) };
  update_descriptor_set(descriptor_set, entries);
  return descriptor_set;
}

// ----------------------------------------------------------------------------

bool RenderContext::load_image_2d(
  CommandEncoder const& cmd,
  std::string_view filename,
  backend::Image &image
) const {
  uint32_t constexpr kForcedChannelCount{ 4u }; //

  bool const is_hdr{ stbi_is_hdr(filename.data()) != 0 };
  bool const is_srgb{ false }; //

  stbi_set_flip_vertically_on_load(false);

  int x, y, num_channels;
  stbi_uc* data{nullptr};

  if (is_hdr) [[unlikely]] {
    data = reinterpret_cast<stbi_uc*>(
      stbi_loadf(filename.data(), &x, &y, &num_channels, kForcedChannelCount) //
    );
  } else {
    data = stbi_load(filename.data(), &x, &y, &num_channels, kForcedChannelCount);
  }

  if (!data) {
    return false;
  }

  VkExtent3D const extent{
    .width = static_cast<uint32_t>(x),
    .height = static_cast<uint32_t>(y),
    .depth = 1u,
  };

  VkFormat const format{ is_hdr ? VK_FORMAT_R32G32B32A32_SFLOAT //
                      : is_srgb ? VK_FORMAT_R8G8B8A8_SRGB
                                : VK_FORMAT_R8G8B8A8_UNORM
  };
  uint32_t const layer_count = 1u;

  image = create_image_2d(
    extent.width,
    extent.height,
    format,
      VK_IMAGE_USAGE_SAMPLED_BIT
    | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    filename
  );

  /* Copy host data to a staging buffer. */
  size_t const comp_bytesize{ (is_hdr ? 4 : 1) * sizeof(std::byte) };
  size_t const bytesize{
    kForcedChannelCount * extent.width * extent.height * comp_bytesize
  };
  auto staging_buffer = allocator().create_staging_buffer(bytesize, data); //
  stbi_image_free(data);

  /* Transfer staging device buffer to image memory. */
  {
    VkImageLayout const transfer_layout{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };
    cmd.transition_images_layout(
      { image },
      VK_IMAGE_LAYOUT_UNDEFINED,
      transfer_layout,
      layer_count
    );

    cmd.copy_buffer_to_image(staging_buffer, image, extent, transfer_layout);

    cmd.transition_images_layout(
      { image },
      transfer_layout,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      layer_count
    );
  }

  return true;
}

// ----------------------------------------------------------------------------

bool RenderContext::load_image_2d(
  std::string_view filename,
  backend::Image& image
) const {
  auto cmd = create_transient_command_encoder();
  bool result = load_image_2d(cmd, filename, image);
  finish_transient_command_encoder(cmd);
  return result;
}

// ----------------------------------------------------------------------------

// GLTFScene RenderContext::load_gltf(
//   std::string_view gltf_filename,
//   scene::Mesh::AttributeLocationMap const& attribute_to_location
// ) {
//   if (auto scene = std::make_shared<GPUResources>(*this); scene) {
//     scene->setup();
//     if (scene->load_file(gltf_filename)) {
//       scene->initialize_submesh_descriptors(attribute_to_location);
//       scene->upload_to_device();
//       return scene;
//     }
//   }

//   return {};
// }

// // ----------------------------------------------------------------------------

// GLTFScene RenderContext::load_gltf(std::string_view gltf_filename) {
//   // -----------------------
//   // -----------------------
//   // [temporary, this should be set elsewhere ideally]
//   static const scene::Mesh::AttributeLocationMap kDefaultFxPipelineAttributeLocationMap{
//     {
//       { Geometry::AttributeType::Position, kAttribLocation_Position },
//       { Geometry::AttributeType::Normal,   kAttribLocation_Normal },
//       { Geometry::AttributeType::Texcoord, kAttribLocation_Texcoord },
//       { Geometry::AttributeType::Tangent,  kAttribLocation_Tangent }, //
//     }
//   };
//   // -----------------------
//   // -----------------------
//   return load_gltf(gltf_filename, kDefaultFxPipelineAttributeLocationMap);
// }

/* -------------------------------------------------------------------------- */
