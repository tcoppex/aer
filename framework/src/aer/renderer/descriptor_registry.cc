#include "aer/renderer/descriptor_registry.h"

#include "aer/renderer/renderer.h"

#include "aer/scene/vertex_internal.h" // (for material_shader_interop)
#include "aer/renderer/fx/skybox.h" //
#include "aer/renderer/raytracing_scene.h" //

/* -------------------------------------------------------------------------- */

/* Allocate the main DescriptorSets. */
void DescriptorRegistry::init(
  Context const& context,
  uint32_t const max_sets
) {
  context_ptr_ = &context;
  device_ = context.device();
  initDescriptorPool(max_sets);
  setupMainDescriptors();
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::release() {
  vkDestroyDescriptorPool(device_, main_pool_, nullptr);
  main_pool_ = VK_NULL_HANDLE;

  for (auto& set : descriptors_) {
    vkDestroyDescriptorSetLayout(device_, set.layout, nullptr);
    set = {};
  }
}

// ----------------------------------------------------------------------------

VkDescriptorSetLayout DescriptorRegistry::createLayout(
  DescriptorSetLayoutParamsBuffer const& params,
  VkDescriptorSetLayoutCreateFlags flags,
  std::string const& name
) const {
  std::vector<VkDescriptorSetLayoutBinding> entries{};
  entries.reserve(params.size());

  std::vector<VkDescriptorBindingFlags> binding_flags{};
  binding_flags.reserve(params.size());

  for (auto const& param : params) {
    entries.push_back({
      .binding = param.binding,
      .descriptorType = param.descriptorType,
      .descriptorCount = param.descriptorCount,
      .stageFlags = param.stageFlags,
      .pImmutableSamplers = param.pImmutableSamplers,
    });
    binding_flags.push_back(param.bindingFlags);
  }

  auto const flags_create_info = VkDescriptorSetLayoutBindingFlagsCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
    .pNext = nullptr,
    .bindingCount = static_cast<uint32_t>(binding_flags.size()),
    .pBindingFlags = binding_flags.data(),
  };

  auto const layout_create_info = VkDescriptorSetLayoutCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = binding_flags.empty() ? nullptr : &flags_create_info,
    .flags = flags,
    .bindingCount = static_cast<uint32_t>(entries.size()),
    .pBindings = entries.data(),
  };

  VkDescriptorSetLayout descriptor_set_layout{};
  CHECK_VK(vkCreateDescriptorSetLayout(
    device_, &layout_create_info, nullptr, &descriptor_set_layout
  ));
  if (!name.empty()) {
    vk_utils::SetDebugObjectName(device_, descriptor_set_layout, "DescriptorRegistry::DescriptorSetLayout::" + name);
  }

  return descriptor_set_layout;
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::destroyLayout(VkDescriptorSetLayout &layout) const {
  vkDestroyDescriptorSetLayout(device_, layout, nullptr);
  layout = VK_NULL_HANDLE;
}

// ----------------------------------------------------------------------------

backend::Buffer DescriptorRegistry::allocateDescriptorBuffer(
  VkDescriptorSetLayout const layout,
  VkDeviceSize *pLayoutSize,
  VkDeviceSize *pOffset,
  uint32_t num_elems,
  VkBufferUsageFlags2KHR usage_flags,
  std::string const& name
) const {
  LOG_CHECK(vkGetDescriptorSetLayoutSizeEXT);
  vkGetDescriptorSetLayoutSizeEXT(device_, layout, pLayoutSize);

  auto const desc_buffer_props = context_ptr_->descriptor_buffer_properties();
  *pLayoutSize = utils::AlignTo(*pLayoutSize, desc_buffer_props.descriptorBufferOffsetAlignment);

  LOG_CHECK(vkGetDescriptorSetLayoutBindingOffsetEXT);
  vkGetDescriptorSetLayoutBindingOffsetEXT(device_, layout, 0u, pOffset);

  auto buffer = context_ptr_->createBuffer(
    *pLayoutSize * num_elems,
      VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
    | usage_flags
    ,
    VMA_MEMORY_USAGE_CPU_TO_GPU
  );

  if (!name.empty()) {
    vk_utils::SetDebugObjectName(device_, buffer.buffer, "DescriptorRegistry::DescriptorSet::" + name);
  }

  return buffer;
}

// ----------------------------------------------------------------------------

VkDescriptorSet DescriptorRegistry::allocateDescriptorSet(
  VkDescriptorSetLayout const layout,
  std::string const& name
) const {
  auto const alloc_info = VkDescriptorSetAllocateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .pNext = nullptr,
    .descriptorPool = main_pool_,
    .descriptorSetCount = 1u,
    .pSetLayouts = &layout,
  };

  VkDescriptorSet descriptor_set{};
  CHECK_VK(vkAllocateDescriptorSets(device_, &alloc_info, &descriptor_set));

  if (!name.empty()) {
    vk_utils::SetDebugObjectName(device_, descriptor_set, "DescriptorRegistry::DescriptorSet::" + name);
  }

  return descriptor_set;
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::bindDescriptorSet(
  Type type,
  GenericCommandEncoder const& cmd,
  VkPipelineLayout pipeline_layout,
  VkShaderStageFlags const stage_flags
) const {
  auto const& desc = descriptor(type);
  cmd.bindDescriptorSet(
    desc.set,
    pipeline_layout,
    stage_flags,
    desc.binding,
    &desc.dynamicOffsets
  );
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::updateFrameUBO(backend::Buffer const& buffer) const {
  context_ptr_->updateDescriptorSet(
    descriptors_[DescriptorRegistry::Type::Frame].set,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_Frame_FrameUBO,
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .buffers = {
          {
            .buffer = buffer.buffer,
            .offset = 0,
            .range = sizeof(material_shader_interop::FrameData)
          }
        },
      }
    }
  );
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::updateSceneTextures(
  std::vector<VkDescriptorImageInfo> image_infos
) const {
  LOG_CHECK(image_infos.size() <= kMaxNumTextures); //

  context_ptr_->updateDescriptorSet(
    descriptors_[DescriptorRegistry::Type::Scene].set,
    {{
      .binding = material_shader_interop::kDescriptorSet_Scene_Textures,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = std::move(image_infos),
    }}
  );
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::updateSceneTexture(
  uint32_t index,
  VkDescriptorImageInfo image_info
) const {
  LOG_CHECK(index <= kMaxNumTextures); //

  context_ptr_->updateDescriptorSet(
    descriptors_[DescriptorRegistry::Type::Scene].set,
    {{
      .binding = material_shader_interop::kDescriptorSet_Scene_Textures,
      .arrayElement = index,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = { image_info },
    }}
  );
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::updateSceneIBL(Skybox const& skybox) const {
  auto const& ibl_sampler = skybox.sampler(); // ClampToEdge Linear MipMap

  context_ptr_->updateDescriptorSet(
    descriptors_[DescriptorRegistry::Type::Scene].set,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_IBL_Prefiltered,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = {
          {
            .sampler = ibl_sampler,
            .imageView = skybox.prefiltered_specular_map().view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          }
        },
      },
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_IBL_Irradiance,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = {
          {
            .sampler = ibl_sampler,
            .imageView = skybox.irradiance_map().view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          }
        },
      },
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_IBL_SpecularBRDF,
        .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .images = {
          {
            .sampler = ibl_sampler,
            .imageView = skybox.specular_brdf_lut().view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          }
        },
      },
    }
  );
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::updateRayTracingScene(RayTracingSceneInterface const* rt_scene) const {
  LOG_CHECK(rt_scene != nullptr);

  auto const& instance_data_buffer = rt_scene->instances_data_buffer();
  LOG_CHECK(instance_data_buffer.valid());

  context_ptr_->updateDescriptorSet(
    descriptors_[DescriptorRegistry::Type::RayTracing].set,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_RayTracing_TLAS,
        .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructures = { rt_scene->tlas().handle },
      },
      {
        .binding = material_shader_interop::kDescriptorSet_RayTracing_InstanceSBO,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffers = { { instance_data_buffer.buffer } },
      },
    }
  );
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::updateFrameUBODynamicOffset(uint32_t offset) const {
  LOG_CHECK(!descriptor(Type::Frame).dynamicOffsets.empty());
  descriptor(Type::Frame).dynamicOffsets[0] = offset;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void DescriptorRegistry::initDescriptorPool(uint32_t const max_sets) {
  /* Default pool, to adjust based on application needs. */
  descriptor_pool_sizes_ = {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 50 },                 // standalone samplers
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2*kMaxNumTextures }, // textures in materials
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 },         // sampled images
    { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 50 },           // compute shaders
    { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 50 },    // texel buffers
    { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 50 },    // storage texel buffers
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 200 },         // per-frame and per-object data
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 100 },         // compute data or large resource buffers
    { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 50 },  // dynamic uniform buffers (per-frame, per-object)
    { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 50 },  // dynamic storage buffers
    { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 50 },        // subpass inputs
    // ---------------------------------------
    { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 50 },
    // ---------------------------------------
  };

  auto const descriptor_pool_info = VkDescriptorPoolCreateInfo{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
    .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT
           | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
           ,
    .maxSets = max_sets,
    .poolSizeCount = static_cast<uint32_t>(descriptor_pool_sizes_.size()),
    .pPoolSizes = descriptor_pool_sizes_.data(),
  };
  CHECK_VK(vkCreateDescriptorPool(
    device_,
    &descriptor_pool_info,
    nullptr, &
    main_pool_
  ));
  vk_utils::SetDebugObjectName(device_, main_pool_, "DescriptorRegistry::MainPool");
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::setupMainDescriptors() {
  auto const extra_stage_flags = VkShaderStageFlags{0
    | VK_SHADER_STAGE_RAYGEN_BIT_KHR
    | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
    | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
  };

  auto const layout_flags = VkDescriptorSetLayoutCreateFlags{0
    | VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
  };

  createMainDescriptorSet(
    Type::Frame,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_Frame_FrameUBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    | extra_stage_flags
                    ,
      },
    },
    layout_flags,
    "Frame"
  );

  createMainDescriptorSet(
    Type::Scene,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_IBL_Prefiltered,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      },
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_IBL_Irradiance,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      },
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_IBL_SpecularBRDF,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      },
      // [Adapt it to have variable count?]
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_Textures,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kMaxNumTextures, //
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    | extra_stage_flags
                    ,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                      // | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT
                      ,
      },
    },
    layout_flags,
    "Scene"
  );

  createMainDescriptorSet(
    Type::RayTracing,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_RayTracing_TLAS,
        .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
                    ,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
      {
        .binding = material_shader_interop::kDescriptorSet_RayTracing_InstanceSBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                    | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
                    ,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
    },
    layout_flags,
    "RayTracing"
  );
}

// ----------------------------------------------------------------------------

DescriptorRegistry::Descriptor& DescriptorRegistry::_intializeMainDescriptor(
  Type const type,
  DescriptorSetLayoutParamsBuffer const& layout_params,
  VkDescriptorSetLayoutCreateFlags layout_flags,
  std::string const& name
) {
  auto& descriptor = descriptors_[type];

  descriptor = {
    .index = static_cast<uint32_t>(type),
    .binding = 0u,
    .layout = createLayout(layout_params, layout_flags, name),
    .set = {},
    .dynamicOffsets = {},
    .layoutSize = 0u,
    .offset = 0u,
  };

  switch (type) {
    case Type::Frame:
      descriptor.binding = material_shader_interop::kDescriptorSet_Frame;
      descriptor.dynamicOffsets = { 0u };
    break;

    case Type::Scene:
      descriptor.binding = material_shader_interop::kDescriptorSet_Scene;
    break;

    case Type::RayTracing:
      descriptor.binding = material_shader_interop::kDescriptorSet_RayTracing;
    break;

    default:
    break;
  }

  return descriptor;
}

// ----------------------------------------------------------------------------

void DescriptorRegistry::createMainDescriptorBuffer(
  Type const type,
  DescriptorSetLayoutParamsBuffer const& layout_params,
  VkDescriptorSetLayoutCreateFlags layout_flags,
  uint32_t num_elems,
  VkBufferUsageFlags2KHR usage_flags,
  std::string const& name
) {
  layout_flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;

  auto &descriptor = _intializeMainDescriptor(type, layout_params, layout_flags, name);

  descriptor.buffer = allocateDescriptorBuffer(
    descriptor.layout,
    &descriptor.layoutSize,
    &descriptor.offset,
    num_elems,
    usage_flags, // eg. VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT
    name
  );
};

// ----------------------------------------------------------------------------

void DescriptorRegistry::createMainDescriptorSet(
  Type const type,
  DescriptorSetLayoutParamsBuffer const& layout_params,
  VkDescriptorSetLayoutCreateFlags layout_flags,
  std::string const& name
) {
  LOG_CHECK(0 == (layout_flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT));

  auto &descriptor = _intializeMainDescriptor(type, layout_params, layout_flags, name);

  descriptor.set = allocateDescriptorSet(descriptor.layout, name);
};

/* -------------------------------------------------------------------------- */
