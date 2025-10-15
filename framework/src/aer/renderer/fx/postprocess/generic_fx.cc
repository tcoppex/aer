#include "aer/renderer/fx/postprocess/generic_fx.h"
#include "aer/renderer/render_context.h"

/* -------------------------------------------------------------------------- */

void GenericFx::init(RenderContext const& context) {
  context_ptr_ = &context;
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
  if (pipeline_layout_ == VK_NULL_HANDLE) {
    return;
  }
  context_ptr_->destroyResources(
    pipeline_,
    pipeline_layout_,
    descriptor_set_layout_
  );
  pipeline_layout_ = VK_NULL_HANDLE;
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

