#ifndef AER_RENDERER_FX_POSTPROCESS_GENERIC_FX_H_
#define AER_RENDERER_FX_POSTPROCESS_GENERIC_FX_H_

#include "aer/renderer/fx/postprocess/fx_interface.h"
#include "aer/renderer/render_context.h"

/* -------------------------------------------------------------------------- */

class GenericFx : public virtual FxInterface {
 public:
  virtual ~GenericFx() = default;

  void init(RenderContext const& context) override;

  void setup(VkExtent2D const dimension) override;

  void release() override;

  void setupUI() override {}

 protected:
  // virtual backend::ShadersMap createShaderModules() const = 0;

  virtual DescriptorSetLayoutParamsBuffer descriptor_set_layout_params() const = 0;

  [[nodiscard]]
  virtual std::vector<VkDescriptorSetLayout> descriptor_set_layouts() const {
    return { descriptor_set_layout_ };
  }

  [[nodiscard]]
  virtual std::vector<VkPushConstantRange> push_constant_ranges() const {
    return {};
  }

  virtual void pushConstant(GenericCommandEncoder const& cmd) const {} //

  virtual void createPipelineLayout();

  virtual void createPipeline() = 0;

 protected:
  RenderContext const* context_ptr_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{}; //
  VkPipelineLayout pipeline_layout_{}; // (redundant, as also kept in pipeline_ when created)

  Pipeline pipeline_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_GENERIC_FX_H_
