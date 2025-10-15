#include <set>

#include "aer/renderer/render_context.h"

#include "aer/platform/swapchain_interface.h" //
#include "aer/scene/image_data.h" // ~
#include "aer/shaders/material/interop.h" // for kAttribLocation_*


/* -------------------------------------------------------------------------- */

namespace {

char const* kDefaulShaderEntryPoint{ "main" };

}

/* -------------------------------------------------------------------------- */

bool RenderContext::init(
  Settings const& settings,
  std::string_view app_name,
  std::vector<char const*> const& instance_extensions,
  XRVulkanInterface *vulkan_xr
) {
  if (!Context::init(app_name, instance_extensions, vulkan_xr)) {
    return false;
  }

  LOGD("-- RenderContext --");

  settings_ = settings;
  settings_.sample_count = static_cast<VkSampleCountFlagBits>(
    static_cast<VkSampleCountFlags>(settings_.sample_count) & sample_counts()
  );

  // (a bit hacky)
  default_view_mask_ = (vulkan_xr != nullptr) ? 0b11u : 0u; //

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

void RenderContext::release() {
  if (device() == VK_NULL_HANDLE) {
    return;
  }

  sampler_pool_.release();
  descriptor_set_registry_.release();
  vkDestroyPipelineCache(device(), pipeline_cache_, nullptr);

  Context::release();
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

std::unique_ptr<RenderTarget> RenderContext::create_default_render_target() const {
  auto desc = RenderTarget::Descriptor{
    .colors = {
      {
        .format = default_color_format(),
        .clear_value = {0.0f, 0.0f, 0.0f, 0.0f},
      },
    },
    .depth_stencil = {
      .format = default_depth_stencil_format(),
      .clear_value = {1u, 0.0f},
    },
    .size = default_surface_size(),
    .array_size = 1u,
    .sample_count = VK_SAMPLE_COUNT_1_BIT, //
  };
  if (default_view_mask_ > 1) {
    desc.array_size = utils::CountBits(default_view_mask_);
  }
  return create_render_target(desc);
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

VkGraphicsPipelineCreateInfo RenderContext::create_graphics_pipeline_create_info(
  GraphicsPipelineCreateInfoData_t &data,
  VkPipelineLayout pipeline_layout,
  GraphicsPipelineDescriptor_t const& desc
) const {
  LOG_CHECK( desc.vertex.module != VK_NULL_HANDLE );
  LOG_CHECK( desc.fragment.module != VK_NULL_HANDLE );

  if (desc.fragment.targets.empty()) {
    LOGW("Fragment targets were not specified for a graphic pipeline.");
  }

  bool const useDynamicRendering{desc.renderPass == VK_NULL_HANDLE};

  data = {};

  // Default color blend attachment.
  data.color_blend_attachments = {
    {
      .blendEnable = VK_FALSE,
      .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
      .colorBlendOp = VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp = VK_BLEND_OP_ADD,

      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                      | VK_COLOR_COMPONENT_G_BIT
                      | VK_COLOR_COMPONENT_B_BIT
                      | VK_COLOR_COMPONENT_A_BIT
                      ,
    }
  };

  /* Dynamic Rendering. */
  if (useDynamicRendering)
  {
    data.color_attachments.resize(desc.fragment.targets.size());
    data.color_blend_attachments.resize(
      data.color_attachments.size(),
      data.color_blend_attachments[0u]
    );

    /* (~) If no depth format is setup, use the default one. */
    VkFormat const depthFormat{
      (desc.depthStencil.format != VK_FORMAT_UNDEFINED) ? desc.depthStencil.format
                                                        : default_depth_stencil_format()
    };
    VkFormat const stencilFormat{
      vk_utils::IsValidStencilFormat(depthFormat) ? depthFormat
                                                  : VK_FORMAT_UNDEFINED
    };

    /* By default we will always use the viewMask of the swapchain. */
    uint32_t const viewMask{
      desc.offscreenSingleView ? 0b0u : default_view_mask()
    };

    for (size_t i = 0; i < data.color_attachments.size(); ++i) {
      auto const& target = desc.fragment.targets[i];

      /* (~) If no color format is setup, use the default one. */
      VkFormat const colorFormat{
        (target.format != VK_FORMAT_UNDEFINED) ? target.format
                                               : default_color_format()
      };

      data.color_attachments[i] = colorFormat;
      data.color_blend_attachments[i] = {
        .blendEnable         = target.blend.enable,
        .srcColorBlendFactor = target.blend.color.srcFactor,
        .dstColorBlendFactor = target.blend.color.dstFactor,
        .colorBlendOp        = target.blend.color.operation,
        .srcAlphaBlendFactor = target.blend.alpha.srcFactor,
        .dstAlphaBlendFactor = target.blend.alpha.dstFactor,
        .alphaBlendOp        = target.blend.alpha.operation,
        .colorWriteMask      = target.writeMask,
      };
    }

    data.dynamic_rendering_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .pNext = nullptr,
      .viewMask = viewMask,
      .colorAttachmentCount = static_cast<uint32_t>(data.color_attachments.size()),
      .pColorAttachmentFormats = data.color_attachments.data(),
      .depthAttachmentFormat = depthFormat,
      .stencilAttachmentFormat = stencilFormat,
    };
  }

  /* Shaders stages */
  auto getShaderEntryPoint{[](std::string const& entryPoint) -> char const* {
    return entryPoint.empty() ? kDefaulShaderEntryPoint : entryPoint.c_str();
  }};

  data.shader_stages = {
    // VERTEX
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .flags = 0,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = desc.vertex.module,
      .pName = getShaderEntryPoint(desc.vertex.entryPoint),
    },
    // FRAGMENT
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .flags = 0,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = desc.fragment.module,
      .pName = getShaderEntryPoint(desc.fragment.entryPoint),
    }
  };

  /* Shader specializations */
  data.specializations.resize(data.shader_stages.size());
  data.shader_stages[0].pSpecializationInfo = data.specializations[0].info(
    desc.vertex.specializationConstants
  );
  data.shader_stages[1].pSpecializationInfo = data.specializations[1].info(
    desc.fragment.specializationConstants
  );

  /* Vertex Input */
  {
    uint32_t binding = 0u;
    for (auto const& buffer : desc.vertex.buffers) {
      data.vertex_bindings.push_back({
        .binding = binding,
        .stride = buffer.stride,
        .inputRate = buffer.inputRate,
      });
      for (auto attrib : buffer.attributes) {
        attrib.binding = binding;
        data.vertex_attributes.push_back(attrib);
      }
      ++binding;
    }

    data.vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount   = static_cast<uint32_t>(data.vertex_bindings.size()),
      .pVertexBindingDescriptions      = data.vertex_bindings.data(),
      .vertexAttributeDescriptionCount = static_cast<uint32_t>(data.vertex_attributes.size()),
      .pVertexAttributeDescriptions    = data.vertex_attributes.data(),
    };
  }

  /* Input Assembly */
  data.input_assembly = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = desc.primitive.topology,
    .primitiveRestartEnable = VK_FALSE,
  };

  /* Tessellation */
  data.tessellation = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
  };

  /* Viewport Scissor */
  data.viewport = {
    .sType          = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    // Viewport and Scissor are set as dynamic, but without VK_EXT_extended_dynamic_state
    // we need to specify the number for each one.
    .viewportCount  = 1u,
    .scissorCount   = 1u,
  };

  /* Rasterization */
  data.rasterization = {
    .sType                    = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .depthClampEnable         = VK_FALSE,
    .rasterizerDiscardEnable  = VK_FALSE,
    .polygonMode              = desc.primitive.polygonMode,
    .cullMode                 = desc.primitive.cullMode,
    .frontFace                = desc.primitive.frontFace,
    .lineWidth                = 1.0f, //
  };

  /* Multisampling */
  auto const sampleCount = (desc.multisample.sampleCount != 0) ? desc.multisample.sampleCount
                                                               : default_sample_count()
                                                               ;
  data.multisample = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples   = sampleCount, //
    .sampleShadingEnable    = VK_FALSE,
    .minSampleShading       = 0.0f,
    .pSampleMask            = nullptr,
    .alphaToCoverageEnable  = VK_FALSE,
    .alphaToOneEnable       = VK_FALSE,
  };

  /* Depth Stencil */
  data.depth_stencil = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable        = desc.depthStencil.depthTestEnable,
    .depthWriteEnable       = desc.depthStencil.depthWriteEnable,
    .depthCompareOp         = desc.depthStencil.depthCompareOp,
    .depthBoundsTestEnable  = VK_FALSE, //
    .stencilTestEnable      = desc.depthStencil.stencilTestEnable,
    .front                  = desc.depthStencil.stencilFront,
    .back                   = desc.depthStencil.stencilBack,
  };

  /* Color Blend */
  data.color_blend = {
    .sType            = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .logicOpEnable    = VK_FALSE,
    .logicOp          = VK_LOGIC_OP_COPY,
    .attachmentCount  = static_cast<uint32_t>(data.color_blend_attachments.size()),
    .pAttachments     = data.color_blend_attachments.data(),
    .blendConstants   = { 0.0f, 0.0f, 0.0f, 0.0f },
  };

  /* Dynamic states */
  {
    // Default states.
    data.dynamic_states = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,

      // VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
      // VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
      // VK_DYNAMIC_STATE_STENCIL_REFERENCE,

      // VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE_EXT,
      // VK_DYNAMIC_STATE_STENCIL_OP_EXT,

      // VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT,
      // VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT,

      // VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY_EXT,
      // VK_DYNAMIC_STATE_CULL_MODE_EXT,

      // VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT,
    };

    // User defined states.
    data.dynamic_states.insert(
      data.dynamic_states.end(), desc.dynamicStates.begin(), desc.dynamicStates.end()
    );

    // Remove dupplicates.
    std::set<VkDynamicState> s(data.dynamic_states.begin(), data.dynamic_states.end());
    data.dynamic_states.assign(s.begin(), s.end());

    data.dynamic_state_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(data.dynamic_states.size()),
      .pDynamicStates = data.dynamic_states.data(),
    };
  }

  auto graphics_pipeline_create_info = VkGraphicsPipelineCreateInfo{
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .flags                = 0,
    .stageCount           = static_cast<uint32_t>(data.shader_stages.size()),
    .pStages              = data.shader_stages.data(),
    .pVertexInputState    = &data.vertex_input,
    .pInputAssemblyState  = &data.input_assembly,
    .pTessellationState   = &data.tessellation,
    .pViewportState       = &data.viewport,
    .pRasterizationState  = &data.rasterization,
    .pMultisampleState    = &data.multisample,
    .pDepthStencilState   = &data.depth_stencil,
    .pColorBlendState     = &data.color_blend,
    .pDynamicState        = &data.dynamic_state_create_info,
    .layout               = pipeline_layout,
    .renderPass           = useDynamicRendering ? VK_NULL_HANDLE : desc.renderPass,
    .subpass              = 0u,
    .basePipelineHandle   = VK_NULL_HANDLE,
    .basePipelineIndex    = -1,
  };

  if (useDynamicRendering) {
    graphics_pipeline_create_info.pNext = &data.dynamic_rendering_create_info;
  }

  return graphics_pipeline_create_info;
}

// ----------------------------------------------------------------------------

void RenderContext::create_graphics_pipelines(
  VkPipelineLayout pipeline_layout,
  std::vector<GraphicsPipelineDescriptor_t> const& descs,
  std::vector<Pipeline> *out_pipelines
) const {
  LOG_CHECK( out_pipelines != nullptr && !out_pipelines->empty() );
  LOG_CHECK( pipeline_layout != VK_NULL_HANDLE );
  LOG_CHECK( !descs.empty() );

  /// When batching pipelines, most underlying data will not changes, so
  /// we could improve setupping by changing only those needed (like
  /// color_blend_attachments).
  std::vector<GraphicsPipelineCreateInfoData_t> datas(descs.size());

  std::vector<VkGraphicsPipelineCreateInfo> create_infos(descs.size());
  for (size_t i = 0; i < descs.size(); ++i) {
    create_infos[i] = create_graphics_pipeline_create_info(
      datas[i],
      pipeline_layout,
      descs[i]
    );
    create_infos[i].flags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    create_infos[i].basePipelineIndex = 0;
  }
  if (!create_infos.empty()) {
    create_infos[0].flags &= ~VK_PIPELINE_CREATE_DERIVATIVE_BIT;
    create_infos[0].flags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
    create_infos[0].basePipelineIndex = -1;
  }

  std::vector<VkPipeline> pipelines(create_infos.size());
  CHECK_VK(vkCreateGraphicsPipelines(
    device(),
    pipeline_cache_,
    create_infos.size(),
    create_infos.data(),
    nullptr,
    pipelines.data()
  ));

  for (size_t i = 0; i < create_infos.size(); ++i) {
    (*out_pipelines)[i] = Pipeline(
      pipeline_layout,
      pipelines[i],
      VK_PIPELINE_BIND_POINT_GRAPHICS
    );
    vk_utils::SetDebugObjectName(
      device(),
      pipelines[i],
      "GraphicsPipeline::NoName_" + std::to_string(i)
    );
  }
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_graphics_pipeline(
  VkPipelineLayout pipeline_layout,
  GraphicsPipelineDescriptor_t const& desc
) const {
  LOG_CHECK( pipeline_layout != VK_NULL_HANDLE );

  GraphicsPipelineCreateInfoData_t data{};
  auto const create_info = create_graphics_pipeline_create_info(
    data, pipeline_layout, desc
  );

  VkPipeline pipeline{};
  CHECK_VK(vkCreateGraphicsPipelines(
    device(), pipeline_cache_, 1u, &create_info, nullptr, &pipeline
  ));
  return Pipeline(pipeline_layout, pipeline, VK_PIPELINE_BIND_POINT_GRAPHICS);
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_graphics_pipeline(
  PipelineLayoutDescriptor_t const& layout_desc,
  GraphicsPipelineDescriptor_t const& desc
) const {
  auto pipeline = create_graphics_pipeline(
    create_pipeline_layout(layout_desc),
    desc
  );
  pipeline.use_internal_layout_ = true;
  return pipeline;
}

// ----------------------------------------------------------------------------

Pipeline RenderContext::create_graphics_pipeline(
  GraphicsPipelineDescriptor_t const& desc
) const {
  return create_graphics_pipeline(
    PipelineLayoutDescriptor_t(),
    desc
  );
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
  auto staging_buffer = create_staging_buffer(bytesize, data); //
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
