#ifndef AER_RENDERER_FX_POSTPROCESS_FRAGMENT_RENDER_TARGET_FX_H_
#define AER_RENDERER_FX_POSTPROCESS_FRAGMENT_RENDER_TARGET_FX_H_

#include "aer/renderer/fx/postprocess/fragment/fragment_fx.h"
#include "aer/renderer/fx/postprocess/post_generic_fx.h"
#include "aer/renderer/targets/render_target.h"

/* -------------------------------------------------------------------------- */

class RenderTargetFx : public FragmentFx
                     , public PostGenericFx {
 public:
  static std::string GetMapScreenVertexShaderName();

 public:
  void release() override;

  void execute(CommandEncoder const& cmd) const override; //

  [[nodiscard]]
  bool resize(VkExtent2D const dimension) override;

  [[nodiscard]]
  backend::Image image_output(uint32_t index = 0u) const override;

  [[nodiscard]]
  virtual std::vector<backend::Image> image_outputs() const override;

  [[nodiscard]]
  backend::Buffer buffer_output(uint32_t index = 0u) const override {
    return {};
  }

  [[nodiscard]]
  std::vector<backend::Buffer> buffer_outputs() const override {
    return {};
  }

 protected:
  virtual void createRenderTarget(VkExtent2D const dimension);

  [[nodiscard]]
  std::string vertex_shader_name() const override {
    return GetMapScreenVertexShaderName();
  }

  [[nodiscard]]
  GraphicsPipelineDescriptor_t graphics_pipeline_descriptor(
    std::vector<backend::ShaderModule> const& shaders
  ) const override;

  [[nodiscard]]
  VkExtent2D surface_size() const override;

  void draw(RenderPassEncoder const& pass) const override {
    pass.draw(3u);
  }

 protected:
  std::shared_ptr<RenderTarget> render_target_{}; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_FRAGMENT_RENDER_TARGET_FX_H_
