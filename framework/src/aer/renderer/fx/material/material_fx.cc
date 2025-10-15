#include "aer/renderer/fx/material/material_fx.h"

#include "aer/platform/vulkan/context.h"
#include "aer/scene/vertex_internal.h" // (for material_shader_interop)

/* -------------------------------------------------------------------------- */

void MaterialFx::init(Renderer const& renderer) {
  renderer_ptr_ = &renderer;
  context_ptr_ = &renderer.context(); //
}

// ----------------------------------------------------------------------------

void MaterialFx::release() {
  if (pipeline_layout_ != VK_NULL_HANDLE) {
    for (auto [_, pipeline] : pipelines_) {
      context_ptr_->destroy_pipeline(pipeline);
    }
    context_ptr_->destroy_pipeline_layout(pipeline_layout_); //
    context_ptr_->destroy_descriptor_set_layout(descriptor_set_layout_);
    pipeline_layout_ = VK_NULL_HANDLE;
  }
}

// ----------------------------------------------------------------------------

void MaterialFx::createPipelines(std::vector<scene::MaterialStates> const& states) {
  auto shaders = createShaderModules();

  // Retrieve specific descriptors.
  std::vector<GraphicsPipelineDescriptor_t> descs{};
  descs.reserve(states.size());
  for (auto const& s : states) {
    descs.push_back( getGraphicsPipelineDescriptor(shaders, s) );
  }

  // Batch create the pipelines.
  std::vector<Pipeline> pipelines(states.size());
  context_ptr_->create_graphics_pipelines(
    pipeline_layout_, descs, &pipelines
  );

  // Store them into the pipeline map.
  for (size_t i = 0; i < states.size(); ++i) {
    pipelines_[states[i]] = pipelines[i];
  }

  for (auto const& [_, shader] : shaders) {
    context_ptr_->release_shader_module(shader);
  }
}

// ----------------------------------------------------------------------------

void MaterialFx::prepareDrawState(
  RenderPassEncoder const& pass,
  scene::MaterialStates const& states
) {
  LOG_CHECK(pipelines_.contains(states));

  pass.bind_pipeline(pipelines_[states]);

  // ----------------------------
  auto const& DSR = context_ptr_->descriptor_set_registry();
  VkShaderStageFlags const stage_flags{
      VK_SHADER_STAGE_VERTEX_BIT
    | VK_SHADER_STAGE_FRAGMENT_BIT
  };

  pass.bind_descriptor_set(
    descriptor_set_,
    pipeline_layout_,
    stage_flags,
    material_shader_interop::kDescriptorSet_Internal
  );

  pass.bind_descriptor_set(
    DSR.descriptor(DescriptorSetRegistry::Type::Frame).set,
    pipeline_layout_,
    stage_flags,
    material_shader_interop::kDescriptorSet_Frame
  );

  pass.bind_descriptor_set(
    DSR.descriptor(DescriptorSetRegistry::Type::Scene).set,
    pipeline_layout_,
    stage_flags,
    material_shader_interop::kDescriptorSet_Scene
  );
  // ----------------------------
}

// ----------------------------------------------------------------------------

void MaterialFx::createPipelineLayout() {
  LOG_CHECK(context_ptr_);

  descriptor_set_layout_ = context_ptr_->create_descriptor_set_layout(
    getDescriptorSetLayoutParams()
  );

  auto const& DSR = context_ptr_->descriptor_set_registry();
  pipeline_layout_ = context_ptr_->create_pipeline_layout({
    .setLayouts = {
      descriptor_set_layout_,
      DSR.descriptor(DescriptorSetRegistry::Type::Frame).layout,
      DSR.descriptor(DescriptorSetRegistry::Type::Scene).layout,
    },
    .pushConstantRanges = getPushConstantRanges()
  });
}

// ----------------------------------------------------------------------------

GraphicsPipelineDescriptor_t MaterialFx::getGraphicsPipelineDescriptor(
  backend::ShaderMap const& shaders,
  scene::MaterialStates const& states
) const {
  LOG_CHECK(shaders.contains(backend::ShaderStage::Vertex));
  LOG_CHECK(shaders.contains(backend::ShaderStage::Fragment));

  GraphicsPipelineDescriptor_t desc{
    .dynamicStates = {
      VK_DYNAMIC_STATE_VERTEX_INPUT_EXT,
      VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,
    },
    .vertex = {
      .module = shaders.at(backend::ShaderStage::Vertex).module,
    },
    .fragment = {
      .module = shaders.at(backend::ShaderStage::Fragment).module,
      .specializationConstants = {
        { 0u, VK_FALSE },  // <=> kUseAlphaCutoff
      },
      .targets = {
        {
          // .format = context_ptr_->default_color_format(), //
        },
      },
    },
    .depthStencil = {
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
    },
    .primitive = {
      .cullMode = VK_CULL_MODE_BACK_BIT,
    }
  };
  if (states.alpha_mode == scene::MaterialStates::AlphaMode::Mask) {
    desc.fragment.specializationConstants[0] = { 0u, VK_TRUE };
  }
  if (states.alpha_mode == scene::MaterialStates::AlphaMode::Blend) {
    desc.fragment.targets[0].blend = {
      .enable = VK_TRUE,
      .color = {
        .operation = VK_BLEND_OP_ADD,
        .srcFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      },
      .alpha =  {
        .operation = VK_BLEND_OP_ADD,
        .srcFactor = VK_BLEND_FACTOR_ONE,
        .dstFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
      },
    };
  }
  return desc;
}

// ----------------------------------------------------------------------------

backend::ShaderMap MaterialFx::createShaderModules() const {
  return {
    {
      backend::ShaderStage::Vertex,
      context_ptr_->create_shader_module(getVertexShaderName())
    },
    {
      backend::ShaderStage::Fragment,
      context_ptr_->create_shader_module(getShaderName())
    },
  };
}

/* -------------------------------------------------------------------------- */
