#include "aer/renderer/fx/postprocess/fragment/render_target_fx.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/renderer.h"

/* -------------------------------------------------------------------------- */

std::string RenderTargetFx::GetMapScreenVertexShaderName() {
  return std::string(FRAMEWORK_COMPILED_SHADERS_DIR "postprocess/mapscreen.vert.glsl");
}

// ----------------------------------------------------------------------------

bool RenderTargetFx::resize(VkExtent2D const dimension) {
  if (!render_target_) {
    createRenderTarget(dimension);
    return true;
  }
  return render_target_->resize(dimension.width, dimension.height);
}

// ----------------------------------------------------------------------------

void RenderTargetFx::release() {
  render_target_->release();
  PostGenericFx::release();
}

// ----------------------------------------------------------------------------

void RenderTargetFx::execute(CommandEncoder const& cmd) const {
  if (!is_enable()) { return; } //

  auto pass = cmd.beginRendering(*render_target_);
  // -----------------------------
  prepareDrawState(pass);
  pushConstant(pass); //
  draw(pass); //
  // -----------------------------
  cmd.endRendering();
}

// ----------------------------------------------------------------------------

backend::Image RenderTargetFx::image_output(uint32_t index) const {
  return render_target_->color_attachment(index); //
}

// ----------------------------------------------------------------------------

std::vector<backend::Image> RenderTargetFx::image_outputs() const {
  return render_target_->color_attachments();
}

// ----------------------------------------------------------------------------

GraphicsPipelineDescriptor_t RenderTargetFx::graphics_pipeline_descriptor(
  std::vector<backend::ShaderModule> const& shaders
) const {
   return {
    // .offscreenSingleView = true,
    .vertex = {
      .module = shaders[0u].module,
    },
    .fragment = {
      .module = shaders[1u].module,
      .targets = {
        { .format = render_target_->color_attachment().format },
      }
    },
    .primitive = {
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      .cullMode = VK_CULL_MODE_BACK_BIT,
    },
    .multisample = {
      .sampleCount = render_target_->sample_count(),
    }
  };
}

// ----------------------------------------------------------------------------

VkExtent2D RenderTargetFx::surface_size() const {
  return render_target_->surface_size();
}

// ----------------------------------------------------------------------------

void RenderTargetFx::createRenderTarget(VkExtent2D const dimension) {
  render_target_ = context_ptr_->createDefaultRenderTarget();
  render_target_->set_color_clear_value({ 0.99f, 0.12f, 0.89f, 0.0f });
}

/* -------------------------------------------------------------------------- */
