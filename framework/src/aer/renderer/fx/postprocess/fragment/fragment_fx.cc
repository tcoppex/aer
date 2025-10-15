#include "aer/renderer/fx/postprocess/fragment/fragment_fx.h"
#include "aer/renderer/render_context.h"

/* -------------------------------------------------------------------------- */

void FragmentFx::setImageInputs(std::vector<backend::Image> const& inputs) {
  DescriptorSetWriteEntry write_entry{
    .binding = 0u,
    .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
  };
  for (auto const& input : inputs) {
    write_entry.images.push_back({
      .sampler = context_ptr_->default_sampler(), //
      .imageView = input.view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    });
  }
  context_ptr_->update_descriptor_set(descriptor_set_, { write_entry });
}

// ----------------------------------------------------------------------------

void FragmentFx::setBufferInputs(std::vector<backend::Buffer> const& inputs) {
  DescriptorSetWriteEntry write_entry{
    .binding = 1u,
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
  };
  for (auto const& input : inputs) {
    write_entry.buffers.push_back({
      .buffer = input.buffer,
      .offset = 0,
      .range = VK_WHOLE_SIZE,
    });
  }
  context_ptr_->update_descriptor_set(descriptor_set_, { write_entry });
}

// ----------------------------------------------------------------------------

void FragmentFx::execute(CommandEncoder const& cmd) const {
  auto pass = cmd.begin_rendering(); //
  {
    prepareDrawState(pass);
    pushConstant(pass); //
    draw(pass);
  }
  cmd.end_rendering();
}

// ----------------------------------------------------------------------------

void FragmentFx::createPipeline() {
  auto shaders{context_ptr_->create_shader_modules({
    getVertexShaderName(),
    getShaderName()
  })};
  pipeline_ = context_ptr_->create_graphics_pipeline(
    pipeline_layout_,
    getGraphicsPipelineDescriptor(shaders)
  );
  context_ptr_->release_shader_modules(shaders);
}

// ----------------------------------------------------------------------------

DescriptorSetLayoutParamsBuffer FragmentFx::getDescriptorSetLayoutParams() const {
  return {
    {
      .binding = kDefaultCombinedImageSamplerBinding,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = kDefaultCombinedImageSamplerDescriptorCount,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    },
    {
      .binding = kDefaultStorageBufferBinding,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = kDefaultStorageBufferDescriptorCount,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
      .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    },
  };
}

// ----------------------------------------------------------------------------

void FragmentFx::prepareDrawState(RenderPassEncoder const& pass) const {
  pass.bind_pipeline(pipeline_);
  pass.bind_descriptor_set(
    descriptor_set_,
    pipeline_layout_,
      VK_SHADER_STAGE_VERTEX_BIT
    | VK_SHADER_STAGE_FRAGMENT_BIT
  );
  pass.set_viewport_scissor(getRenderSurfaceSize()); //
}

/* -------------------------------------------------------------------------- */
