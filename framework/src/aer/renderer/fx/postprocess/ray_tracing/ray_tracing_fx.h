#ifndef AER_RENDERER_FX_PP_RT_RAY_TRACING_FX_H_
#define AER_RENDERER_FX_PP_RT_RAY_TRACING_FX_H_

#include "aer/renderer/fx/postprocess/post_generic_fx.h"
#include "aer/platform/vulkan/accel_struct.h"
#include "aer/renderer/ray_tracing_view.h"

/* -------------------------------------------------------------------------- */

/**
 * A PostProcessing Fx using Ray Tracing shaders.
 *
 * There are some similarities with MaterialFx but the both of them are
 * decorrelated as for now.
 */
class RayTracingFx : public PostGenericFx {
 public:
  // -------------------------------
  static constexpr uint kDescriptorSetBinding_RayTracing_AccumImage      = 0;
  // -------------------------------

 public:
  void release() override {
    context_ptr_->destroyBuffer(sbt_storage_buffer_);
    releaseOutputImagesAndBuffers();
    GenericFx::release();
  }

  void setup(VkExtent2D const dimension) override {
    PostGenericFx::setup(dimension);
    
    context_ptr_->updateDescriptorSet(descriptor_set_, {
      {
        .binding = kDescriptorSetBinding_RayTracing_AccumImage,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .images = {
          {
            .imageView = out_images_[0].view,
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
          }
        },
      },
    });
    resetFrameAccumulation();
  }

  void execute(CommandEncoder const& cmd) const override;

  virtual void resetFrameAccumulation() = 0;

  virtual void set_view(RayTracingView const& view) {
    view_ = view;
  }

  [[nodiscard]]
  RayTracingView const& view() const noexcept {
    return view_;
  }

 public:
  bool resize(VkExtent2D const dimension) override;

  [[nodiscard]]
  backend::Image image_output(uint32_t index = 0u) const final {
    return out_images_[index];
  }

  [[nodiscard]]
  std::vector<backend::Image> /*const&*/ image_outputs() const final {
    return out_images_;
  }

  [[nodiscard]]
  backend::Buffer buffer_output(uint32_t index = 0u) const final {
    return out_buffers_[index];
  }

  [[nodiscard]]
  std::vector<backend::Buffer> /*const&*/ buffer_outputs() const final {
    return out_buffers_;
  }

  void set_image_inputs(std::vector<backend::Image> const& inputs) override {} //

  void set_buffer_inputs(std::vector<backend::Buffer> const& inputs) override {} //

 protected:
  virtual void resetMemoryBarriers();

  [[nodiscard]]
  DescriptorSetLayoutParamsBuffer descriptor_set_layout_params() const override {
    return {
      // [use pushDescriptor instead ?]
      {
        .binding = kDescriptorSetBinding_RayTracing_AccumImage,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
    };
  }

  [[nodiscard]]
  std::vector<VkDescriptorSetLayout> descriptor_set_layouts() const override {
    auto const& registry = context_ptr_->descriptor_registry();
    return {
      descriptor_set_layout_,
      registry.descriptor(DescriptorRegistry::Type::Scene).layout,
      registry.descriptor(DescriptorRegistry::Type::RayTracing).layout,
    };
  }

  [[nodiscard]]
  virtual backend::ShadersMap createShaderModules() const = 0;

  [[nodiscard]]
  virtual RayTracingPipelineDescriptor_t pipelineDescriptor(backend::ShadersMap const& shaders_map) = 0;

  void createPipeline() override;

  virtual void buildShaderBindingTable(RayTracingPipelineDescriptor_t const& desc);

  virtual void releaseOutputImagesAndBuffers() {
    for (auto &image : out_images_) {
      context_ptr_->destroyImage(image);
    }
    out_images_.clear();

    for (auto &buffer : out_buffers_) {
      context_ptr_->destroyBuffer(buffer);
    }
    out_buffers_.clear();
  }

 protected:
  VkExtent2D dimension_{};
  std::vector<backend::Image> out_images_{};
  std::vector<backend::Buffer> out_buffers_{};

  struct {
    std::vector<VkImageMemoryBarrier2> images_start{};
    std::vector<VkImageMemoryBarrier2> images_end{};
  } barriers_;

  backend::Buffer sbt_storage_buffer_{};
  backend::RayTracingAddressRegion region_{};

  RayTracingView view_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_PP_RT_RAY_TRACING_FX_H_