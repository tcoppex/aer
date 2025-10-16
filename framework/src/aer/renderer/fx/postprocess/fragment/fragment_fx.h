#ifndef AER_RENDERER_FX_FRAGMENT_FX_H_
#define AER_RENDERER_FX_FRAGMENT_FX_H_

#include "aer/renderer/fx/postprocess/generic_fx.h"

/* -------------------------------------------------------------------------- */

class FragmentFx : public virtual GenericFx {
 public:
  static constexpr uint32_t kDefaultCombinedImageSamplerBinding{ 0u };
  static constexpr uint32_t kDefaultStorageBufferBinding{ 1u };

  static constexpr uint32_t kDefaultCombinedImageSamplerDescriptorCount{ 8u };
  static constexpr uint32_t kDefaultStorageBufferDescriptorCount{ 4u };

 public:
  void set_image_inputs(std::vector<backend::Image> const& inputs) override;

  void set_buffer_inputs(std::vector<backend::Buffer> const& inputs) override;

  void execute(CommandEncoder const& cmd) const override;

 protected:
  void createPipeline() override;

  [[nodiscard]]
  DescriptorSetLayoutParamsBuffer descriptor_set_layout_params() const override;

 protected:
  virtual std::string vertex_shader_name() const = 0;

  virtual std::string shader_name() const = 0;

  virtual GraphicsPipelineDescriptor_t graphics_pipeline_descriptor(
    std::vector<backend::ShaderModule> const& shaders
  ) const = 0;

  virtual VkExtent2D surface_size() const = 0;

  virtual void prepareDrawState(RenderPassEncoder const& pass) const;

  virtual void draw(RenderPassEncoder const& pass) const = 0; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_FRAGMENT_FX_H_
