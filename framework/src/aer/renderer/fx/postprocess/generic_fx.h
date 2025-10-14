#ifndef AER_RENDERER_FX_POSTPROCESS_GENERIC_FX_H_
#define AER_RENDERER_FX_POSTPROCESS_GENERIC_FX_H_

#include "aer/renderer/fx/postprocess/fx_interface.h"
#include "aer/renderer/pipeline.h"

/* -------------------------------------------------------------------------- */

class GenericFx : public virtual FxInterface {
 public:
  virtual ~GenericFx() = default;

  void init(Renderer const& renderer) override;

  void setup(VkExtent2D const dimension) override;

  void release() override;

  void setupUI() override {}

 protected:
  // virtual backend::ShadersMap createShaderModules() const = 0;

  virtual DescriptorSetLayoutParamsBuffer getDescriptorSetLayoutParams() const = 0;

  virtual std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts() const {
    return {descriptor_set_layout_};
  }

  virtual std::vector<VkPushConstantRange> getPushConstantRanges() const {
    return {};
  }

  virtual void pushConstant(GenericCommandEncoder const& cmd) const {} //

  virtual void createPipelineLayout();

  virtual void createPipeline() = 0;

 protected:
  RenderContext const* context_ptr_{};
  Renderer const* renderer_ptr_{};

  // ----------------
  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{}; //
  VkPipelineLayout pipeline_layout_{}; // (redundant, as also kept in pipeline_ when created)
  // ----------------

  Pipeline pipeline_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_GENERIC_FX_H_
