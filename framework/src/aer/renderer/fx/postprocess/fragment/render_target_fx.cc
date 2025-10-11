#include "aer/renderer/fx/postprocess/fragment/render_target_fx.h"
#include "aer/platform/backend/command_encoder.h"
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

void RenderTargetFx::execute(CommandEncoder& cmd) const {
  if (!enabled()) { return; } //

  // -----------------------------
  auto pass = cmd.begin_rendering(*render_target_);
  prepareDrawState(pass);
  pushConstant(pass); //

  draw(pass); //

  cmd.end_rendering();
  // -----------------------------
}

// ----------------------------------------------------------------------------

backend::Image RenderTargetFx::getImageOutput(uint32_t index) const {
  return render_target_->color_attachment(index); //
}

// ----------------------------------------------------------------------------

std::vector<backend::Image> RenderTargetFx::getImageOutputs() const {
  return render_target_->color_attachments();
}

// ----------------------------------------------------------------------------

GraphicsPipelineDescriptor_t RenderTargetFx::getGraphicsPipelineDescriptor(
  std::vector<backend::ShaderModule> const& shaders
) const {
   return {
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

VkExtent2D RenderTargetFx::getRenderSurfaceSize() const {
  return render_target_->surface_size();
}

// ----------------------------------------------------------------------------

void RenderTargetFx::createRenderTarget(VkExtent2D const dimension) {
  VkClearColorValue const debug_clear_value{ { 0.99f, 0.12f, 0.89f, 0.0f } }; //

#if 1
  render_target_ = renderer_ptr_->create_default_render_target();
#else
  render_target_ = context_ptr_->create_render_target({
    .colors = {
      { .format = VK_FORMAT_R32G32B32A32_SFLOAT },
    },
    .depth_stencil = {
      .format = VK_FORMAT_D24_UNORM_S8_UINT, //
    },
    .size = dimension,
    // .sample_count = VK_SAMPLE_COUNT_1_BIT,
  });
#endif

  render_target_->set_color_clear_value(debug_clear_value);
}

/* -------------------------------------------------------------------------- */
