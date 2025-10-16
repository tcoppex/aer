#ifndef AER_RENDERER_FX_MATERIAL_MATERIAL_FX_H_
#define AER_RENDERER_FX_MATERIAL_MATERIAL_FX_H_

#include "aer/core/common.h"
#include "aer/scene/material.h"

#include "aer/renderer/render_context.h"

/* -------------------------------------------------------------------------- */

class MaterialFx {
 public:
  MaterialFx() = default;
  
  virtual ~MaterialFx() = default;

  virtual void init(RenderContext const& context);

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
  virtual std::string vertex_shader_name() const = 0;

  virtual std::string shader_name() const = 0;

  [[nodiscard]]
  virtual backend::ShaderMap createShaderModules() const;

  virtual DescriptorSetLayoutParamsBuffer descriptor_set_layout_params() const {
    return {};
  }

  virtual std::vector<VkPushConstantRange> push_constant_ranges() const {
    return {};
  }

  virtual void createPipelineLayout();

  virtual void createDescriptorSets() {
    descriptor_set_ = context_ptr_->createDescriptorSet(descriptor_set_layout_); //
  }

 protected:
  [[nodiscard]]
  virtual GraphicsPipelineDescriptor_t graphics_pipeline_descriptor(
    backend::ShaderMap const& shaders,
    scene::MaterialStates const& states
  ) const;

 public:
  // -- frame-wide resource descriptor --

  // -- mesh instance push constants --

  virtual void set_transform_index(uint32_t index) = 0;
  virtual void set_material_index(uint32_t index) = 0;
  virtual void set_instance_index(uint32_t index) = 0;

  // -- material utils --

  virtual uint32_t createMaterial(scene::MaterialProxy const& material_proxy) = 0;

  virtual void pushMaterialStorageBuffer() const = 0;

 protected:
  RenderContext const* context_ptr_{};

  VkDescriptorSetLayout descriptor_set_layout_{};
  VkDescriptorSet descriptor_set_{}; //
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
    context_ptr_->destroyBuffer(material_storage_buffer_);
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
      context_ptr_->writeBuffer(
        material_storage_buffer_,
        materials_.data(),
        materials_.size() * sizeof(ShaderMaterial)
      );
    } else {
      context_ptr_->transientUploadBuffer(
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
      material_storage_buffer_ = context_ptr_->createBuffer(
        buffersize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
      | VMA_ALLOCATION_CREATE_MAPPED_BIT
      );
    } else {
      // Setup the SSBO for rarer device-to-device transfer.
      material_storage_buffer_ = context_ptr_->createBuffer(
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
