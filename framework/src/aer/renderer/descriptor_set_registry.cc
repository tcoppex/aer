#include "aer/renderer/descriptor_set_registry.h"

#include "aer/renderer/renderer.h"

#include "aer/scene/vertex_internal.h" // (for material_shader_interop)
#include "aer/renderer/fx/skybox.h" //
#include "aer/renderer/raytracing_scene.h" //

/* -------------------------------------------------------------------------- */

/* Allocate the main DescriptorSets. */
void DescriptorSetRegistry::init(
  Context const& context,
  uint32_t const max_sets
) {
  context_ptr_ = &context;
  device_ = context.device();
  init_descriptor_pool(max_sets);
  init_descriptor_sets();
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::release() {
  for (auto& set : sets_) {
    vkDestroyDescriptorSetLayout(device_, set.layout, nullptr);
    set = {};
  }
  vkDestroyDescriptorPool(device_, main_pool_, nullptr);
}

// ----------------------------------------------------------------------------

VkDescriptorSetLayout DescriptorSetRegistry::create_layout(
  DescriptorSetLayoutParamsBuffer const& params,
  VkDescriptorSetLayoutCreateFlags flags
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

  VkDescriptorSetLayoutBindingFlagsCreateInfo const flags_create_info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
    .bindingCount = static_cast<uint32_t>(binding_flags.size()),
    .pBindingFlags = binding_flags.data(),
  };
  VkDescriptorSetLayoutCreateInfo const layout_create_info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    .pNext = binding_flags.empty() ? nullptr : &flags_create_info,
    .flags = flags,
    .bindingCount = static_cast<uint32_t>(entries.size()),
    .pBindings = entries.data(),
  };

  VkDescriptorSetLayout descriptor_set_layout;
  CHECK_VK(vkCreateDescriptorSetLayout(
    device_, &layout_create_info, nullptr, &descriptor_set_layout
  ));

  return descriptor_set_layout;
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::destroy_layout(VkDescriptorSetLayout &layout) const {
  vkDestroyDescriptorSetLayout(device_, layout, nullptr);
  layout = VK_NULL_HANDLE;
}

// ----------------------------------------------------------------------------

VkDescriptorSet DescriptorSetRegistry::allocate_descriptor_set(
  VkDescriptorSetLayout const layout
) const {
  VkDescriptorSetAllocateInfo const alloc_info{
    .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
    .descriptorPool = main_pool_,
    .descriptorSetCount = 1u,
    .pSetLayouts = &layout,
  };
  VkDescriptorSet descriptor_set{};
  CHECK_VK(vkAllocateDescriptorSets(device_, &alloc_info, &descriptor_set));
  return descriptor_set;
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::update_frame_ubo(backend::Buffer const& buffer) const {
  context_ptr_->update_descriptor_set(
    sets_[DescriptorSetRegistry::Type::Frame].set,
  {{
    .binding = material_shader_interop::kDescriptorSet_Frame_FrameUBO,
    .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    .buffers = { { buffer.buffer } },
  }});
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::update_scene_transforms(backend::Buffer const& buffer) const {
  context_ptr_->update_descriptor_set(
    sets_[DescriptorSetRegistry::Type::Scene].set,
  {{
    .binding = material_shader_interop::kDescriptorSet_Scene_TransformSBO,
    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    .buffers = { { buffer.buffer } },
  }});
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::update_scene_textures(std::vector<VkDescriptorImageInfo> image_infos) const {
  context_ptr_->update_descriptor_set(
    sets_[DescriptorSetRegistry::Type::Scene].set,
    {{
      .binding = material_shader_interop::kDescriptorSet_Scene_Textures,
      .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .images = std::move(image_infos),
    }}
  );
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::update_scene_ibl(Skybox const& skybox) const {
  auto const& ibl_sampler = skybox.sampler(); // ClampToEdge Linear MipMap

  context_ptr_->update_descriptor_set(
    sets_[DescriptorSetRegistry::Type::Scene].set,
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

void DescriptorSetRegistry::update_ray_tracing_scene(RayTracingSceneInterface const* rt_scene) const {
  context_ptr_->update_descriptor_set(
    sets_[DescriptorSetRegistry::Type::RayTracing].set,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_RayTracing_TLAS,
        .type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        .accelerationStructures = { rt_scene->tlas().handle },
      },
      {
        .binding = material_shader_interop::kDescriptorSet_RayTracing_InstanceSBO,
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .buffers = { { rt_scene->instances_data_buffer().buffer } },
      },
    }
  );
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void DescriptorSetRegistry::init_descriptor_pool(uint32_t const max_sets) {
  /* Default pool, to adjust based on application needs. */
  descriptor_pool_sizes_ = {
    { VK_DESCRIPTOR_TYPE_SAMPLER, 50 },                 // standalone samplers
    { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 }, // textures in materials
    { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 },          // sampled images
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

  VkDescriptorPoolCreateInfo const descriptor_pool_info{
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
  vk_utils::SetDebugObjectName(device_, main_pool_, "DescriptorSetRegistry::MainPool");
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::init_descriptor_sets() {
  VkShaderStageFlags extra_stage_flags{
      VK_SHADER_STAGE_RAYGEN_BIT_KHR
    | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
    | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
  };

  create_main_set(
    Type::Frame,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_Frame_FrameUBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    | extra_stage_flags
                    ,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      },
    },
    VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
    "Frame"
  );

  create_main_set(
    Type::Scene,
    {
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_TransformSBO,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1u,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                      | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT
                      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
      },
      {
        .binding = material_shader_interop::kDescriptorSet_Scene_Textures,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = kMaxNumTextures, //
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT
                    | VK_SHADER_STAGE_FRAGMENT_BIT
                    | extra_stage_flags
                    ,
        .bindingFlags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
                      | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
      },
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
    },
    VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
    "Scene"
  );

  create_main_set(
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
    VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
    "RayTracing"
  );
}

// ----------------------------------------------------------------------------

void DescriptorSetRegistry::create_main_set(
  Type const type,
  DescriptorSetLayoutParamsBuffer const& layout_params,
  VkDescriptorSetLayoutCreateFlags layout_flags,
  std::string const& name
) {
  VkDescriptorSetLayout const layout = create_layout(layout_params, layout_flags);
  sets_[type] = {
    .index = static_cast<uint32_t>(type),
    .set = allocate_descriptor_set(layout),
    .layout = layout,
  };

  vk_utils::SetDebugObjectName(device_, sets_[type].set,    "DescriptorSetRegistry::DescriptorSet::" + name);
  vk_utils::SetDebugObjectName(device_, sets_[type].layout, "DescriptorSetRegistry::DescriptorSetLayout::" + name);
};

/* -------------------------------------------------------------------------- */
