#include "aer/renderer/fx/postprocess/fragment/fragment_fx.h"
#include "aer/renderer/renderer.h"
#include "aer/platform/backend/command_encoder.h"

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

void FragmentFx::execute(CommandEncoder& cmd) const {
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
  pipeline_ = renderer_ptr_->create_graphics_pipeline(
    pipeline_layout_,
    getGraphicsPipelineDescriptor(shaders)
  );
  context_ptr_->release_shader_modules(shaders);
}

// ----------------------------------------------------------------------------

VkExtent2D FragmentFx::getRenderSurfaceSize() const {
  return renderer_ptr_->surface_size();
}

/* -------------------------------------------------------------------------- */
