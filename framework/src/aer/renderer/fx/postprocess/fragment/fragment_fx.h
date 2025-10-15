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
  void setImageInputs(std::vector<backend::Image> const& inputs) override;

  void setBufferInputs(std::vector<backend::Buffer> const& inputs) override;

  void execute(CommandEncoder const& cmd) const override; //

 protected:
  // [deprecated]
  virtual std::string getShaderName() const = 0; //

  void createPipeline() override;

  DescriptorSetLayoutParamsBuffer getDescriptorSetLayoutParams() const override {
    return {
      {
        .binding = kDefaultCombinedImageSamplerBinding,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kDefaultCombinedImageSamplerDescriptorCount,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
      {
        .binding = kDefaultStorageBufferBinding,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = kDefaultStorageBufferDescriptorCount,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
    };
  }

  virtual VkExtent2D getRenderSurfaceSize() const;

  virtual void prepareDrawState(RenderPassEncoder const& pass) const {
    pass.bind_pipeline(pipeline_);
    pass.bind_descriptor_set(
      descriptor_set_,
      pipeline_layout_,
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT
    );
    pass.set_viewport_scissor(getRenderSurfaceSize()); //
  }

 protected:
  virtual std::string getVertexShaderName() const = 0;

  virtual GraphicsPipelineDescriptor_t getGraphicsPipelineDescriptor(
    std::vector<backend::ShaderModule> const& shaders
  ) const = 0;

  virtual void draw(RenderPassEncoder const& pass) const = 0; //
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_FRAGMENT_FX_H_
