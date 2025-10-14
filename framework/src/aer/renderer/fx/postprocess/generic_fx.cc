#include "aer/renderer/fx/postprocess/generic_fx.h"

#include "aer/renderer/render_context.h"
#include "aer/renderer/renderer.h"

/* -------------------------------------------------------------------------- */

void GenericFx::init(Renderer const& renderer) {
  context_ptr_ = &renderer.context();
  renderer_ptr_ = &renderer;
}

// ----------------------------------------------------------------------------

void GenericFx::setup(VkExtent2D const dimension) {
  LOG_CHECK(nullptr != context_ptr_);
  createPipelineLayout();
  createPipeline();
  descriptor_set_ = context_ptr_->create_descriptor_set(descriptor_set_layout_); //
}

// ----------------------------------------------------------------------------

void GenericFx::release() {
  if (pipeline_layout_ != VK_NULL_HANDLE) {
    context_ptr_->destroy_pipeline(pipeline_);
    context_ptr_->destroy_pipeline_layout(pipeline_layout_); //
    context_ptr_->destroy_descriptor_set_layout(descriptor_set_layout_);
    pipeline_layout_ = VK_NULL_HANDLE;
  }
}

// ----------------------------------------------------------------------------

// std::string GenericFx::name() const {
//   std::filesystem::path fn(getShaderName());
//   return fn.stem().string();
// }

// ----------------------------------------------------------------------------

void GenericFx::createPipelineLayout() {
  descriptor_set_layout_ = context_ptr_->create_descriptor_set_layout(
    getDescriptorSetLayoutParams()
  );
  pipeline_layout_ = context_ptr_->create_pipeline_layout({
    .setLayouts = getDescriptorSetLayouts(),
    .pushConstantRanges = getPushConstantRanges()
  });
}

/* -------------------------------------------------------------------------- */

