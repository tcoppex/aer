#ifndef AER_RENDERER_FX_PP_RT_RAY_TRACING_FX_H_
#define AER_RENDERER_FX_PP_RT_RAY_TRACING_FX_H_

#include "aer/renderer/fx/postprocess/post_generic_fx.h"
#include "aer/renderer/renderer.h" //

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
  static constexpr uint kDescriptorSetBinding_ImageOutput      = 0;
  static constexpr uint kDescriptorSetBinding_MaterialSBO      = 1;
  // -------------------------------

 public:
  void release() override {
    context_ptr_->destroy_buffer(material_storage_buffer_);
    context_ptr_->destroy_buffer(sbt_storage_buffer_);
    releaseOutputImagesAndBuffers();
    GenericFx::release();
  }

  void setup(VkExtent2D const dimension) override {
    PostGenericFx::setup(dimension);
    
    context_ptr_->update_descriptor_set(descriptor_set_, {
      {
        .binding = kDescriptorSetBinding_ImageOutput,
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

  void execute(CommandEncoder& cmd) const override;

  void buildMaterialStorageBuffer(
    std::vector<scene::MaterialProxy> const& proxy_materials
  ) {
    buildMaterials(proxy_materials);
    if (size_t bufferSize = getMaterialBufferSize(); bufferSize > 0) {
      // Setup the SSBO for rarer device-to-device transfer.
      material_storage_buffer_ = context_ptr_->create_buffer(
        bufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
      );

      context_ptr_->transfer_host_to_device(
        getMaterialBufferData(),
        bufferSize,
        material_storage_buffer_
      );

      context_ptr_->update_descriptor_set(descriptor_set_, {
        {
          .binding = kDescriptorSetBinding_MaterialSBO,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .buffers = { { material_storage_buffer_.buffer } },
        },
      });

      // (we could release the internal material buffer here)
    }
  }

  virtual void resetFrameAccumulation() = 0;

 public:
  bool resize(VkExtent2D const dimension) override;

  backend::Image getImageOutput(uint32_t index = 0u) const final {
    return out_images_[index];
  }

  std::vector<backend::Image> /*const&*/ getImageOutputs() const final {
    return out_images_;
  }

  backend::Buffer getBufferOutput(uint32_t index = 0u) const final {
    return out_buffers_[index];
  }

  std::vector<backend::Buffer> /*const&*/ getBufferOutputs() const final {
    return out_buffers_;
  }

  void setImageInputs(std::vector<backend::Image> const& inputs) override {} //

  void setBufferInputs(std::vector<backend::Buffer> const& inputs) override {} //

 protected:
  virtual void resetMemoryBarriers();

  DescriptorSetLayoutParamsBuffer getDescriptorSetLayoutParams() const override {
    return {
      {
        .binding = kDescriptorSetBinding_ImageOutput, //
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
      {
        .binding = kDescriptorSetBinding_MaterialSBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                    | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
                    ,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
    };
  }

  std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts() const override {
    auto const& DSR = context_ptr_->descriptor_set_registry();
    return {
      descriptor_set_layout_,
      DSR.descriptor(DescriptorSetRegistry::Type::Frame).layout,
      DSR.descriptor(DescriptorSetRegistry::Type::Scene).layout,
      DSR.descriptor(DescriptorSetRegistry::Type::RayTracing).layout,
    };
  }

  virtual backend::ShadersMap createShaderModules() const = 0;

  virtual RayTracingPipelineDescriptor_t pipelineDescriptor(backend::ShadersMap const& shaders_map) = 0;

  void createPipeline() override;

  virtual void buildShaderBindingTable(RayTracingPipelineDescriptor_t const& desc);

  virtual void releaseOutputImagesAndBuffers() {
    for (auto &image : out_images_) {
      context_ptr_->destroy_image(image);
    }
    out_images_.clear();

    for (auto &buffer : out_buffers_) {
      context_ptr_->destroy_buffer(buffer);
    }
    out_buffers_.clear();
  }

  // ----------------------------
  // Build optionnal ray tracing materials based on proxy materials.
  virtual void buildMaterials(std::vector<scene::MaterialProxy> const& proxy_materials) {
  }

  virtual void const* getMaterialBufferData() const {
    return nullptr;
  }

  virtual size_t getMaterialBufferSize() const {
    return 0;
  }
  // ----------------------------

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

  backend::Buffer material_storage_buffer_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_PP_RT_RAY_TRACING_FX_H_