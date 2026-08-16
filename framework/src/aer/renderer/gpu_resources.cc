#include "aer/renderer/gpu_resources.h"

#include "aer/core/camera.h"
#include "aer/renderer/render_context.h"
#include "aer/renderer/fx/material/material_fx.h"
#include "aer/renderer/fx/postprocess/ray_tracing/ray_tracing_fx.h" //
#include "aer/scene/vertex_internal.h" // for material_shader_interop::FrameData

using namespace scene;

/* -------------------------------------------------------------------------- */

GPUResources::GPUResources(
  RenderContext const& context,
  uint32_t max_frames_in_flight
)
  : context_(context)
  , max_frames_in_flight_(max_frames_in_flight)
{
  material_fx_registry_ = std::make_unique<MaterialFxRegistry>();
  material_fx_registry_->init(context_);
}

// ----------------------------------------------------------------------------

GPUResources::~GPUResources() {
  context_.deviceWaitIdle();

  for (auto& img : device_images) {
    context_.destroyImage(img);
  }
  context_.destroyBuffer(transforms_sbo_);
  context_.destroyBuffer(frame_sbo_);
  context_.destroyBuffer(index_buffer);
  context_.destroyBuffer(vertex_buffer);

  // ---------------------------------------
  rt_scene_.reset();
  // ---------------------------------------

  if (material_fx_registry_) {
    material_fx_registry_->release();
    material_fx_registry_.reset();
  }
}

// ----------------------------------------------------------------------------

bool GPUResources::loadFile(std::string_view filename) {
  if (!HostResources::loadFile(filename)) {
    return false;
  }

  /* Force a specific material model when requested. */
  {
    auto const material_model = context_.default_material_model();
    if (material_model != scene::MaterialModel::Unknown) {
      for (auto const& material_ref : material_refs) {
        material_ref->model = material_model;
      }
    }
  }

  return true;
}

// ----------------------------------------------------------------------------

void GPUResources::initializeSubmeshDescriptors(
  Mesh::AttributeLocationMap const& attribute_to_location
) {
  for (auto& mesh : meshes) {
    mesh->initializeSubmeshDescriptors(attribute_to_location);
  }

  // --------------------
  // [~] When we expect Tangent we force recalculate them.
  //     Resulting indices might be incorrect.
  if (attribute_to_location.contains(Geometry::AttributeType::Tangent)) {
    // for (auto& mesh : meshes) { mesh->recalculateTangents(); } //
  }
  // --------------------
}

// ----------------------------------------------------------------------------

void GPUResources::uploadToDevice(UploadFlags const flags) {
  bool const bUseRayTracing = 0 < (flags & kUploadFlagBits_BuildRayTracingData);
  bool const bReleaseHostDataOnUpload = 0 < (flags & kUploadFlagBits_ReleaseHostDataOnUpload);

  /* Force descriptors to be up to date before uploading.
     Will invalidate previous ones.
  */
  resetInternalDescriptors();

  /* Build the Material Registry. */
  {
    material_fx_registry_->setup(material_proxies, material_refs); //
    material_fx_registry_->uploadMaterialStorageBuffers();
  }

  /* Initialize the RayTracing data structure. */
  if (bUseRayTracing) {
    rt_scene_ = std::make_unique<RayTracingScene>();
    rt_scene_->init(context_);
  }

  /* Create the shared Frame SBO */
  if (!frame_sbo_.valid()) {
    // -----------------------------------
    // Create a ring SBO for frame data.
    // Need to be aligned to VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment
    VkDeviceSize const min_alignment = context_.gpu_properties()
      .limits.minUniformBufferOffsetAlignment;
    frame_data_stride_ = utils::AlignTo(
      sizeof(material_shader_interop::FrameData), min_alignment
    );
    uint32_t const total_buffer_size = frame_data_stride_ * max_frames_in_flight_;
    // -----------------------------------

    frame_sbo_ = context_.createBuffer(
      total_buffer_size,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      ,
      VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
  }

  /* Transfer Textures */
  if (total_image_size > 0) {
    uploadImages();
  }

  /* Transfer Buffers */
  if (vertex_buffer_size > 0) {
    uploadBuffers();

    /* Build the Raytracing acceleration structures. */
    if (bUseRayTracing) {
      // (The global matrices buffer should have been initialized to build the BLAS).
      // updateTransformsBuffer();

      rt_scene_->build(meshes, transforms, vertex_buffer, index_buffer);
      ray_tracing_fx_->set_instance_buffer_address(rt_scene_->instances_data_buffer().address);
      ray_tracing_fx_->set_tlas_address(rt_scene_->tlas().address);
    }
  }

  /* Clear host data once uploaded. */
  if (bReleaseHostDataOnUpload) {
    host_images.clear();
    host_images.shrink_to_fit();
    for (auto const& mesh : meshes) {
      mesh->clearIndicesAndVertices(); //
    }
  }

  /* Initial descriptor setup */
  updateGlobalDescriptorSetBindings(); //
}

// ----------------------------------------------------------------------------

std::vector<VkDescriptorImageInfo> GPUResources::buildDescriptorImageInfos() const {
  std::vector<VkDescriptorImageInfo> image_infos{};

  if (textures.empty()) {
    return image_infos;
  }
  image_infos.reserve(textures.size());

  auto const& sampler_pool = context_.sampler_pool();
  for (auto const& texture : textures) {
    auto const& img = device_images.at(texture.channel_index());
    image_infos.push_back({
      .sampler = sampler_pool.convert(texture.sampler),
      .imageView = img.view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    });
  }
  return image_infos;
}

// ----------------------------------------------------------------------------

void GPUResources::update(Camera const& camera, float elapsed_time) {
  // [CPU bound]

  /* Recalculate the whole hierarchy global transform buffer. */
  updateTransformsBuffer(); // (to check / rename)

  /* Prepare the scenes for rasterization (sort meshes). */
  if (!ray_tracing_fx_ || !ray_tracing_fx_->is_enable()) {
    prepareRasterizationRendering(camera);
  }

  // ------------

  // [GPU bound]

  /* Update and upload per-frame data. */
  updateFrameData(camera, elapsed_time); // (also upload, decorelate ?)

  /* Upload mesh transforms when needed. */
  uploadTransforms();
};

// ----------------------------------------------------------------------------

void GPUResources::render(RenderPassEncoder const& pass) {
  LOG_CHECK( material_fx_registry_ != nullptr );
  LOG_CHECK( !material_refs.empty() ); //

  if (ray_tracing_fx_ && ray_tracing_fx_->is_enable()) {
    return;
  }

  uint32_t instance_index = 0u;
  for (auto& lookup : lookups_) {
    for (auto& [hashpair, submeshes] : lookup) {
      auto [fx, states] = hashpair;

      auto const material_buffer_address = fx->material_buffer_address();

      // Bind pipeline & descriptor set.
      fx->prepareDrawState(pass, states);

      // Draw submeshes.
      for (auto submesh : submeshes) {
        auto mesh = submesh->parent;
        auto const& matref = *(submesh->material_ref);
        auto const& proxy = material_proxy(matref);

        // Submesh's MaterialFx pushConstants.
        // --------------------------
        fx->set_push_constant_generic({
          .frame_buffer_address = frame_data_current_address_,
          .transform_buffer_address = transforms_sbo_.address,
          .material_buffer_address = material_buffer_address,
          // -----
          .transform_index = mesh->transform_index,
          .material_index = matref.material_index,
          .instance_index = instance_index++,
        });
        fx->pushConstant(pass);
        // --------------------------

        pass.setPrimitiveTopology(mesh->vk_primitive_topology());
        pass.setCullMode(proxy.double_sided ? VK_CULL_MODE_NONE
                                            : VK_CULL_MODE_BACK_BIT);

        pass.bindAndDraw(submesh->draw_descriptor, vertex_buffer, index_buffer);
      }
    }
  }
}

// ----------------------------------------------------------------------------

void GPUResources::setupRayTracingFx(RayTracingFx* fx) {
  LOG_CHECK(fx != nullptr);
  fx->buildMaterialStorageBuffer(material_proxies); //
  ray_tracing_fx_ = fx;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void GPUResources::updateGlobalDescriptorSetBindings() const {
  auto const& registry = context_.descriptor_registry();

  if (total_image_size > 0) {
    registry.updateSceneTextures(buildDescriptorImageInfos());
  }

  // ---------------------------------------
  if (rt_scene_ && (vertex_buffer_size > 0)) {
    registry.updateRayTracingScene(rt_scene_.get());
  }
  // ---------------------------------------
}

// ----------------------------------------------------------------------------

void GPUResources::uploadImages() {
  LOG_CHECK( total_image_size > 0 );

  /* Create a staging buffer. */
  backend::Buffer staging_buffer{
    context_.createStagingBuffer( total_image_size ) //
  };

  device_images.reserve(host_images.size()); //

  std::vector<VkBufferImageCopy> copies{};
  copies.reserve(host_images.size());

  uint64_t staging_offset = 0lu;
  uint32_t const layer_count = 1u;
  for (auto const& host_image : host_images) {
    auto const extent = VkExtent3D{
      .width = static_cast<uint32_t>(host_image.width),
      .height = static_cast<uint32_t>(host_image.height),
      .depth = 1u,
    };
    device_images.push_back(context_.createImage2D(
      extent.width,
      extent.height,
      VK_FORMAT_R8G8B8A8_UNORM, //
        VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ));

    /* Upload image to staging buffer */
    auto const img_bytesize = host_image.bytesize();
    context_.writeBuffer(
      staging_buffer, staging_offset, host_image.pixels(), 0u, img_bytesize
    );
    copies.push_back({
      .bufferOffset = staging_offset,
      .imageSubresource = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .layerCount = layer_count,
      },
      .imageExtent = extent,
    });
    staging_offset += img_bytesize;
  }

  auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Transfer);
  {
    VkImageLayout const transfer_layout{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };

    cmd.transitionColorImages(
      device_images,
      VK_IMAGE_LAYOUT_UNDEFINED,
      transfer_layout,
      layer_count
    );
    for (uint32_t i = 0u; i < device_images.size(); ++i) {
      vkCmdCopyBufferToImage(
        cmd.handle(),
        staging_buffer.buffer,
        device_images[i].image,
        transfer_layout,
        1u,
        &copies[i]
      );
    }
    cmd.transitionColorImages(
      device_images,
      transfer_layout,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      layer_count
    );
  }
  context_.finishTransientCommandEncoder(cmd);
}

// ----------------------------------------------------------------------------

void GPUResources::uploadBuffers() {
  LOG_CHECK(vertex_buffer_size > 0);
  LOG_CHECK(transforms.size() == meshes.size()); //

  VkBufferUsageFlags extra_flags{};

  // ---------------------------------------
  if (rt_scene_) {
    extra_flags = extra_flags
      // Position & Indices are needed for the BLAS.
      | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      // Attributes & Indices are fetched by the closeshit shaders.
      | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT //
      ;
  }
  // ---------------------------------------

  /* Allocate device buffers for meshes & their transforms. */
  vertex_buffer = context_.createBuffer(
    vertex_buffer_size,
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
    | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR
    | extra_flags
    ,
    VMA_MEMORY_USAGE_GPU_ONLY
  );

  if (index_buffer_size > 0) {
    index_buffer = context_.createBuffer(
      index_buffer_size,
        VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT
      | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR
      | extra_flags
      ,
      VMA_MEMORY_USAGE_GPU_ONLY
    );
  }

  // Meshes transforms buffer.
  size_t const transforms_buffer_size{ transforms.size() * sizeof(transforms[0]) };
  {
    // -----------------------------
    // [NOTEs]
    // - we might want to separate static vs dynamic transforms
    // - when update frequently, this would require max_frames_in_flights buffering
    transforms_sbo_ = context_.createBuffer(
      transforms_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT //
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      ,
      VMA_MEMORY_USAGE_CPU_TO_GPU
    );
    // -----------------------------
  }

  /* Copy host mesh data to the staging buffer. */
  auto staging_buffer = context_.createStagingBuffer(
    vertex_buffer_size + index_buffer_size + transforms_buffer_size
  );
  {
    std::byte* device_data{};
    size_t vertex_offset{0lu};
    size_t index_offset{vertex_buffer_size};

    context_.mapMemory(staging_buffer, (void**)&device_data);

    // Transfer the attributes & indices by ranges.
    for (auto const& mesh : meshes) {
      auto const& vertices = mesh->vertices();
      memcpy(device_data + vertex_offset, vertices.data(), vertices.size());
      vertex_offset += vertices.size();

      if (index_buffer_size > 0) {
        auto const& indices = mesh->indices();
        memcpy(device_data + index_offset, indices.data(), indices.size());
        index_offset += indices.size();
      }
    }

    // Transfer the transforms buffer in one go.
    // (discarded as it will be transfered later on)
    if constexpr (false) {
      memcpy(
        device_data + vertex_buffer_size + index_buffer_size,
        transforms.data(),
        transforms_buffer_size
      );
    }

    context_.unmapMemory(staging_buffer);
  }

  /* Copy device data from staging buffers to their respective buffers. */
  auto cmd = context_.createTransientCommandEncoder(Context::TargetQueue::Transfer);
  {
    size_t src_offset{0lu};
    src_offset = cmd.copyBuffer(
      staging_buffer, src_offset, vertex_buffer, 0u, vertex_buffer_size
    );
    if (index_buffer_size > 0) {
      src_offset = cmd.copyBuffer(
        staging_buffer, src_offset, index_buffer, 0u, index_buffer_size
      );
    }
    src_offset = cmd.copyBuffer(
      staging_buffer, src_offset, transforms_sbo_, 0u, transforms_buffer_size
    );

    std::vector<VkBufferMemoryBarrier2> barriers{
      {
        .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .buffer = vertex_buffer.buffer,
        .size = vertex_buffer_size,
      },
      {
        .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, //
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .buffer = transforms_sbo_.buffer,
        .size = transforms_buffer_size,
      },
    };
    if (index_buffer_size > 0) {
      barriers.push_back({
        .srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        .buffer = index_buffer.buffer,
        .size = index_buffer_size,
      });
    }
    cmd.pipelineBufferBarriers(barriers);
  }
  context_.finishTransientCommandEncoder(cmd);
}

// ----------------------------------------------------------------------------

void GPUResources::uploadTransforms() {
  LOG_CHECK(transforms.size() == meshes.size()); //
#if 1
  context_.writeBuffer(transforms_sbo_, transforms); // require mapping ability
#else
  context_.transientUploadBuffer(transforms, transforms_sbo_);
#endif
}

// ----------------------------------------------------------------------------

void GPUResources::updateFrameData(Camera const& camera, float elapsed_time) {

  /* Current surface size provided by the Renderer to the RenderContext,
   * in the future this might need tweaking if we use scaling. */
  auto const& surface_size = context_.default_surface_size();

  auto frame_data = material_shader_interop::FrameData{
    .default_world_matrix = context_.default_world_matrix(),
    .cameraPos_Time = vec4(camera.position(), elapsed_time),
    .resolution = vec2(surface_size.width, surface_size.height),
    .frame = frame_index_,
    .renderer_states = 0b11111111111111111111111111111111, //
  };
  LOGW("FrameData.renderer_states use a default value, "\
       "its irradiance bit should be set by the Renderer::Skybox object state.");

  /* Copy the multiview CameraTransform. */
  {
    static_assert(std::is_trivially_copyable_v<Camera::Transform>);
    auto& dst = frame_data.cameras;
    auto const& src = camera.transforms();
    std::memcpy(dst, (void*)src.data(), sizeof(Camera::Transform) * src.size());
  }

  /* Upload frame data to the device. */
  LOG_CHECK(max_frames_in_flight_ > 0);
  LOG_CHECK(frame_data_stride_ > 0);

  uint32_t const current_slot = frame_index_ % max_frames_in_flight_;
  size_t const offset = current_slot * frame_data_stride_;
  context_.writeBuffer(frame_sbo_, offset, &frame_data, 0u, sizeof(frame_data));

  // Update the cycling Frame Buffer address.
  frame_data_current_address_ = frame_sbo_.address + offset;

  // As ray traced scenes might be rendered externally we update the ir
  // frame buffer address directly.
  if (rt_scene_ && ray_tracing_fx_) {
    ray_tracing_fx_->set_frame_buffer_address(frame_data_current_address_);
  }

  // (probably not the best place to be updated)
  ++frame_index_; //
}

// ----------------------------------------------------------------------------

void GPUResources::prepareRasterizationRendering(Camera const& camera) {
  LOG_CHECK(!ray_tracing_fx_ || !ray_tracing_fx_->is_enable());

  // -- Retrieve submeshes associated to each MaterialFx --

  if constexpr (true) {
    lookups_ = {};
    for (auto const& mesh : meshes) {
      for (auto const& submesh : mesh->submeshes) {
        if (auto matref = submesh.material_ref; matref) {
          auto const alpha_mode = matref->states.alpha_mode;
          auto fx = material_fx_registry_->material_fx(*matref);
          auto hashpair = std::make_pair(fx, matref->states);
          lookups_[alpha_mode][hashpair].emplace_back(&submesh);
        }
      }
    }
    //reset_scene_lookups = false;
  }

  // -- Sort each buffer of submeshes --

  using SortKey = std::pair<float, size_t>; // (depthProxy, index)
  std::vector<SortKey> sortkeys{};
  SubMeshBuffer swap_buffer{};
  auto const camera_dir = camera.direction();

  auto sort_submeshes = [&](SubMeshBuffer &submeshes, auto comp) {
    sortkeys = {};
    sortkeys.reserve(submeshes.size());
    for (size_t i = 0; i < submeshes.size(); ++i) {
      mat4 const& world = transforms[submeshes[i]->parent->transform_index];
      vec3 const pos = lina::to_vec3(world.w);
      vec3 const v = camera.position() - pos;
      float const dp = lina::dot(camera_dir, v);
      sortkeys.emplace_back(dp, i);
    }
    std::ranges::sort(sortkeys, comp, &SortKey::first);

    // final-sort on submeshes by swapping with new buffer.
    swap_buffer.resize(submeshes.size());
    for (size_t i = 0; i < submeshes.size(); ++i) {
      auto [_, submesh_index] = sortkeys[i];
      swap_buffer[i] = std::move(submeshes[submesh_index]);
    }
    submeshes.swap(swap_buffer);
  };

  // -- [optionnal] Sort front to back for early depth testing --

  for (auto& [_, submeshes] : lookups_[MaterialStates::AlphaMode::Opaque]) {
    sort_submeshes(submeshes, std::less{});
  }

  if constexpr (false) {
    for (auto& [_, submeshes] : lookups_[MaterialStates::AlphaMode::Mask]) {
      sort_submeshes(submeshes, std::less{});
    }
  }

  // -- Sort back to front for alpha blending --

  for (auto& [_, submeshes] : lookups_[MaterialStates::AlphaMode::Blend]) {
    sort_submeshes(submeshes, std::greater{});
  }
}

/* -------------------------------------------------------------------------- */
