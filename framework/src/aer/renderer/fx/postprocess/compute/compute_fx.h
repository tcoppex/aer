#ifndef AER_RENDERER_FX_POSTPROCESS_COMPUTE_COMPUTE_FX_H_
#define AER_RENDERER_FX_POSTPROCESS_COMPUTE_COMPUTE_FX_H_

#include "aer/renderer/fx/postprocess/post_generic_fx.h"

/* -------------------------------------------------------------------------- */

class ComputeFx : public PostGenericFx {
 public:
  // (should be defined in a shared interop.h)
  static constexpr uint32_t kDefaultStorageImageBindingInput{ 0u }; //
  static constexpr uint32_t kDefaultStorageBufferBindingInput{ 1u }; //

  static constexpr uint32_t kDefaultStorageImageBindingOutput{ 2u }; //
  static constexpr uint32_t kDefaultStorageBufferBindingOutput{ 3u }; //

  static constexpr uint32_t kDefaultDescriptorStorageImageCount{ 4u }; //
  static constexpr uint32_t kDefaultDescriptorStorageBufferCount{ 4u }; //

 public:
  bool resize(VkExtent2D const dimension) override {
    LOG_CHECK(dimension.width > 0);
    LOG_CHECK(dimension.height > 0);
    if ( (dimension.width == dimension_.width)
      && (dimension.height == dimension_.height)
      && (!images_.empty() || !buffers_.empty())
      ) {
      return false;
    }
    dimension_ = dimension;
    return true;
  }

  void release() override;

  void execute(CommandEncoder& cmd) const override;

  // --- Setters ---

  void setImageInputs(std::vector<backend::Image> const& inputs) override;

  void setBufferInputs(std::vector<backend::Buffer> const& inputs) override;

  // TODO
  // void setImageOutputs(std::vector<backend::Image> const& inputs) override;
  // void setBufferOutputs(std::vector<backend::Buffer> const& inputs) override;

  // --- Getters ---

  backend::Image getImageOutput(uint32_t index = 0u) const override {
    LOG_CHECK(index < images_.size());
    return images_[index];
  }

  std::vector<backend::Image> getImageOutputs() const override {
    return images_;
  }

  backend::Buffer getBufferOutput(uint32_t index = 0u) const override {
    LOG_CHECK(index < buffers_.size());
    return buffers_[index];
  }

  std::vector<backend::Buffer> getBufferOutputs() const override {
    return buffers_;
  }

 protected:
  virtual void releaseImagesAndBuffers();
  
  // [deprecated]
  virtual std::string getShaderName() const = 0; //

  DescriptorSetLayoutParamsBuffer getDescriptorSetLayoutParams() const override {
    return {
      // INPUTS
      {
        .binding = kDefaultStorageImageBindingInput,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = kDefaultDescriptorStorageImageCount,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
      {
        .binding = kDefaultStorageBufferBindingInput,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = kDefaultDescriptorStorageBufferCount,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
      // OUTPUTS
      {
        .binding = kDefaultStorageImageBindingOutput,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = kDefaultDescriptorStorageImageCount,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
      {
        .binding = kDefaultStorageBufferBindingOutput,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = kDefaultDescriptorStorageBufferCount,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
    };
  }

  void createPipeline() override;

 protected:
  VkExtent2D dimension_{}; //

  // Outputs.
  std::vector<backend::Image> images_{};
  std::vector<backend::Buffer> buffers_{};
};

// ----------------------------------------------------------------------------

// [move to context?]
// template<typename PushConstantT>
// std::tuple< std::vector<backend::Image>, std::vector<backend::Buffer> >
// RunSingleComputeKernel(
//   std::string_view shader_path,
//   PushConstantT const& push_constant,
//   std::vector<backend::Image> const& input_images,
//   std::vector<backend::Buffer> const& input_buffers,
//   std::vector<backend::Image> const& output_images,
//   std::vector<backend::Buffer> const& ouput_buffers
// );

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_POSTPROCESS_COMPUTE_COMPUTE_FX_H_
