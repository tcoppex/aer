#ifndef AER_RENDERER_FX_MATERIAL_MATERIAL_FX_H_
#define AER_RENDERER_FX_MATERIAL_MATERIAL_FX_H_

#include "aer/core/common.h"
#include "aer/scene/material.h"

#include "aer/renderer/render_context.h"
#include "aer/renderer/renderer.h"

/* -------------------------------------------------------------------------- */

class MaterialFx {
 public:
  MaterialFx() = default;
  
  virtual ~MaterialFx() = default;

  virtual void init(Renderer const& renderer);

  virtual void setup() {
    createPipelineLayout();
    createDescriptorSets();
  }

  virtual void release();

  virtual void createPipelines(std::vector<scene::MaterialStates> const& states);

  virtual void prepareDrawState(
    RenderPassEncoder const& pass,
    scene::MaterialStates const& states
  );

  virtual void pushConstant(GenericCommandEncoder const& cmd) = 0;

  /* Check if the MaterialFx has been setup. */
  bool valid() const {
    return pipeline_layout_ != VK_NULL_HANDLE;
  }

 protected:
  virtual std::string getVertexShaderName() const = 0;

  virtual std::string getShaderName() const = 0;

  virtual backend::ShaderMap createShaderModules() const;

  virtual DescriptorSetLayoutParamsBuffer getDescriptorSetLayoutParams() const {
    return {};
  }

  virtual std::vector<VkPushConstantRange> getPushConstantRanges() const {
    return {};
  }

  virtual void createPipelineLayout();

  virtual void createDescriptorSets() {
    descriptor_set_ = context_ptr_->create_descriptor_set(descriptor_set_layout_); //
  }

 protected:
  virtual GraphicsPipelineDescriptor_t getGraphicsPipelineDescriptor(
    backend::ShaderMap const& shaders,
    scene::MaterialStates const& states
  ) const;

 public:
  // -- frame-wide resource descriptor --

  // -- mesh instance push constants --

  virtual void setTransformIndex(uint32_t index) = 0;
  virtual void setMaterialIndex(uint32_t index) = 0;
  virtual void setInstanceIndex(uint32_t index) = 0;

  // -- material utils --

  virtual uint32_t createMaterial(scene::MaterialProxy const& material_proxy) = 0;

  virtual void pushMaterialStorageBuffer() const = 0;

 protected:
  RenderContext const* context_ptr_{};
  Renderer const* renderer_ptr_{};

  // ----------------
  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{}; //
  // ----------------
  VkPipelineLayout pipeline_layout_{}; //

  std::map<scene::MaterialStates, Pipeline> pipelines_{};
  backend::Buffer material_storage_buffer_{};
};

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

template<typename MaterialT>
class TMaterialFx : public MaterialFx {
 public:
  using ShaderMaterial = MaterialT;

  static constexpr uint32_t kDefaultMaterialCount{ 1024u }; //
  static constexpr bool kEditMode{ false };

 public:
  void setup() override {
    MaterialFx::setup();
    setupMaterialStorageBuffer();
  }

  void release() override {
    context_ptr_->allocator().destroy_buffer(material_storage_buffer_);
    MaterialFx::release();
  }

  uint32_t createMaterial(scene::MaterialProxy const& material_proxy) final {
    materials_.emplace_back( convertMaterialProxy(material_proxy) );
    return  static_cast<uint32_t>(materials_.size() - 1u);
  }

  void pushMaterialStorageBuffer() const override {
    LOG_CHECK(materials_.size() < kDefaultMaterialCount);

    if (materials_.empty()) {
      return;
    }

    // ------------------------------
    if constexpr (kEditMode) {
      context_ptr_->allocator().write_buffer(
        material_storage_buffer_,
        materials_.data(),
        materials_.size() * sizeof(ShaderMaterial)
      );
    } else {
      context_ptr_->transfer_host_to_device(
        materials_.data(),
        materials_.size() * sizeof(ShaderMaterial),
        material_storage_buffer_
      );
    }
    // ------------------------------
  }

  ShaderMaterial const& material(uint32_t index) const {
    return materials_[index];
  }

 private:
  virtual ShaderMaterial convertMaterialProxy(scene::MaterialProxy const& proxy) const = 0;

  void setupMaterialStorageBuffer() {
    size_t const buffersize = kDefaultMaterialCount * sizeof(ShaderMaterial);
    materials_.reserve(kDefaultMaterialCount);

    if constexpr (kEditMode) {
      // Setup the SSBO for frequent host-device mapping (slower).
      material_storage_buffer_ = context_ptr_->allocator().create_buffer(
        buffersize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
      | VMA_ALLOCATION_CREATE_MAPPED_BIT
      );
    } else {
      // Setup the SSBO for rarer device-to-device transfer.
      material_storage_buffer_ = context_ptr_->allocator().create_buffer(
        buffersize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY
      );
    }
  }

 protected:
  std::vector<ShaderMaterial> materials_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_FX_MATERIAL_MATERIAL_FX_H_
