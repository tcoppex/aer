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

  bool resize(VkExtent2D const dimension) override;

  backend::Image getImageOutput(uint32_t index = 0u) const override;

  virtual std::vector<backend::Image> getImageOutputs() const override;

  backend::Buffer getBufferOutput(uint32_t index = 0u) const override {
    return {};
  }

  std::vector<backend::Buffer> getBufferOutputs() const override {
    return {};
  }

 protected:
  virtual void createRenderTarget(VkExtent2D const dimension);

  std::string getVertexShaderName() const override {
    return GetMapScreenVertexShaderName();
  }

  GraphicsPipelineDescriptor_t getGraphicsPipelineDescriptor(
    std::vector<backend::ShaderModule> const& shaders
  ) const override;

  VkExtent2D getRenderSurfaceSize() const override;

  void draw(RenderPassEncoder const& pass) const override {
    pass.draw(3u);
  }

 protected:
  std::shared_ptr<RenderTarget> render_target_{}; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_FRAGMENT_RENDER_TARGET_FX_H_
