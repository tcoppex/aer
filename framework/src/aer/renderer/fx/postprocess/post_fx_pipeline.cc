#include "aer/renderer/fx/postprocess/post_fx_pipeline.h"
#include "aer/platform/vulkan/context.h"
#include "aer/renderer/renderer.h"

/* -------------------------------------------------------------------------- */

void PostFxPipeline::init(Renderer const& renderer) {
  context_ptr_ = &renderer.context();
  renderer_ptr_ = &renderer;

  LOG_CHECK(!effects_.empty());
  for (auto fx : effects_) {
    fx->init(renderer);
  }
}

void PostFxPipeline::setupDependencies() {
  LOG_CHECK(context_ptr_ != nullptr);
  LOG_CHECK(!effects_.empty());

  context_ptr_->device_wait_idle();

  for (size_t i = 0; i < effects_.size(); ++i) {
    auto& fx = effects_[i];
    auto const& dep = dependencies_[i];

    if (!dep.images.empty()) {
      std::vector<backend::Image> input_images;
      input_images.reserve(dep.images.size());
      for (auto const& [image_fx, index] : dep.images) {
        // LOGD("fx {}, image {} {}", i, index, image_fx->name());
        input_images.push_back(image_fx->getImageOutput(index));
      }
      fx->setImageInputs(std::move(input_images));
    }

    if (!dep.buffers.empty()) {
      std::vector<backend::Buffer> input_buffers;
      input_buffers.reserve(dep.buffers.size());
      for (auto const& [buffer_fx, index] : dep.buffers) {
        // LOGD("fx {}, buffer {} {}", i, index, buffer_fx->name());
        input_buffers.push_back(buffer_fx->getBufferOutput(index));
      }
      fx->setBufferInputs(std::move(input_buffers));
    }
  }
}

/* -------------------------------------------------------------------------- */
