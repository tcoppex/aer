#include "aer/renderer/gpu_resources.h"

#include "aer/core/camera.h"
#include "aer/renderer/renderer.h"
#include "aer/renderer/fx/material/material_fx.h"

#include "aer/renderer/fx/postprocess/ray_tracing/ray_tracing_fx.h" //
#include "aer/shaders/material/interop.h" //

using namespace scene;

/* -------------------------------------------------------------------------- */

GPUResources::GPUResources(Renderer const& renderer)
  : renderer_ptr_(&renderer)
  , context_ptr_(&renderer.context())
{
  // ---------------------------------------
  rt_scene_ = std::make_unique<RayTracingScene>();
  rt_scene_->init(*context_ptr_); //
  // ---------------------------------------
}

// ----------------------------------------------------------------------------

GPUResources::~GPUResources() {
  context_ptr_->device_wait_idle();

  if (material_fx_registry_) {
    material_fx_registry_->release();
  }

  // ---------------------------------------
  rt_scene_.reset();
  // ---------------------------------------

  for (auto& img : device_images) {
    context_ptr_->destroy_image(img);
  }
  context_ptr_->destroy_buffer(transforms_ssbo_);
  context_ptr_->destroy_buffer(frame_ubo_);
  context_ptr_->destroy_buffer(index_buffer);
  context_ptr_->destroy_buffer(vertex_buffer);
}

// ----------------------------------------------------------------------------

bool GPUResources::load_file(std::string_view filename) {
  if (!HostResources::load_file(filename)) {
    return false;
  }

  material_fx_registry_ = std::make_unique<MaterialFxRegistry>();
  material_fx_registry_->init(*renderer_ptr_);
  material_fx_registry_->setup(material_proxies, material_refs);

  return true;
}

// ----------------------------------------------------------------------------

void GPUResources::initialize_submesh_descriptors(Mesh::AttributeLocationMap const& attribute_to_location) {
  for (auto& mesh : meshes) {
    mesh->initialize_submesh_descriptors(attribute_to_location);
  }

  // --------------------
  // [~] When we expect Tangent we force recalculate them.
  //     Resulting indices might be incorrect.
  if (attribute_to_location.contains(Geometry::AttributeType::Tangent)) {
    // for (auto& mesh : meshes) { mesh->recalculate_tangents(); } //
  }
  // --------------------
}

// ----------------------------------------------------------------------------

void GPUResources::upload_to_device(bool const bReleaseHostDataOnUpload) {
  /* Transfer Materials */
  material_fx_registry_->push_material_storage_buffers();

  /* Create the shared Frame UBO */
  frame_ubo_ = context_ptr_->create_buffer(
    sizeof(FrameData),
      VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ,
    VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
  );

  /* Transfer Textures */
  if (total_image_size > 0) {
    upload_images(*context_ptr_);
  }

  /* Transfer Buffers */
  if (vertex_buffer_size > 0) {
    upload_buffers(*context_ptr_);

    // ---------------------------------------
    /* Build the Raytracing acceleration structures. */
    if (rt_scene_) {
      rt_scene_->build(meshes, vertex_buffer, index_buffer);
    }
    // ---------------------------------------
  }

  /* Update Global Descriptor Set bindings. */
  {
    auto const& DSR = context_ptr_->descriptor_set_registry();

    DSR.update_frame_ubo(frame_ubo_);

    if (total_image_size > 0) {
      DSR.update_scene_textures(descriptor_image_infos());
    }

    DSR.update_scene_transforms(transforms_ssbo_);

    // ---------------------------------------
    if (rt_scene_) {
      DSR.update_ray_tracing_scene(rt_scene_.get());
    }
    // ---------------------------------------
  }

  /* Clear host data once uploaded */
  if (bReleaseHostDataOnUpload) {
    host_images.clear();
    host_images.shrink_to_fit();
    for (auto const& mesh : meshes) {
      mesh->clear_indices_and_vertices(); //
    }
  }
}

// ----------------------------------------------------------------------------

std::vector<VkDescriptorImageInfo> GPUResources::descriptor_image_infos() const {
  std::vector<VkDescriptorImageInfo> image_infos{};

  if (textures.empty()) {
    return image_infos;
  }
  image_infos.reserve(textures.size());

  auto const& sampler_pool = context_ptr_->sampler_pool();
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

void GPUResources::update(
  Camera const& camera,
  VkExtent2D const& surfaceSize,
  float elapsedTime
) {
  update_frame_data(camera, surfaceSize, elapsedTime);

  if (ray_tracing_fx_ && ray_tracing_fx_->enabled()) {
    return;
  }

  /// ---------------------------------------
  ///
  /// + DevNotes +
  ///
  /// We could improve the overall sorting by using a generic 64bits sorting
  /// key (pipeline << 32) | (material << 16) | depthBits), using a single buffer.
  ///
  /// The lookups_ are bins sorted by AlphaMode, to avoid collisions on possible
  /// future MaterialStates we hash the map on <MaterialFx*, MaterialStates> pairs
  /// instead of the sole MaterialFx*, however the resulting key not being sort
  /// it would possible to rebind a pipeline multiple time for a given lookup bins,
  /// which is okay for alphablend materials but could affect performance on
  /// Opaque / AlphaMask renders.
  /// However using std::map with MaterialFx* as the first key of the pair should
  /// give ordered result.
  ///
  /// ---------------------------------------

  // Retrieve submeshes associated to each MaterialFx.
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

  // Sort each buffer of submeshes.

  using SortKey = std::pair<float, size_t>; // (depthProxy, index)
  std::vector<SortKey> sortkeys{};
  SubMeshBuffer swap_buffer{};
  auto const camera_dir = camera.direction();

  auto sort_submeshes = [&](SubMeshBuffer &submeshes, auto comp) {
    sortkeys = {};
    sortkeys.reserve(submeshes.size());
    for (size_t i = 0; i < submeshes.size(); ++i) {
      mat4 const& world = submeshes[i]->parent->world_matrix();
      vec3 const pos = lina::to_vec3(world.w);
      vec3 const v = camera.position() - pos;
      float const dp = linalg::dot(camera_dir, v);
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

  // Sort front to back for depth testing.
  for (auto& [_, submeshes] : lookups_[MaterialStates::AlphaMode::Opaque]) {
    sort_submeshes(submeshes, std::less{});
  }
  for (auto& [_, submeshes] : lookups_[MaterialStates::AlphaMode::Mask]) {
    sort_submeshes(submeshes, std::less{});
  }
  // Sort back to front for alpha blending.
  for (auto& [_, submeshes] : lookups_[MaterialStates::AlphaMode::Blend]) {
    sort_submeshes(submeshes, std::greater{});
  }
}

// ----------------------------------------------------------------------------

void GPUResources::render(RenderPassEncoder const& pass) {
  LOG_CHECK( material_fx_registry_ != nullptr );

  if (ray_tracing_fx_ && ray_tracing_fx_->enabled()) {
    return;
  }

  // Render each Fx.
  uint32_t instance_index = 0u;
  for (auto& lookup : lookups_) {
    for (auto& [ hashpair, submeshes] : lookup) {
      auto [fx, states] = hashpair;

      // Bind pipeline & descriptor set.
      // auto const& states = submeshes[0]->material_ref->states;
      fx->prepareDrawState(pass, states);

      // Draw submeshes.
      for (auto submesh : submeshes) {
        auto mesh = submesh->parent;

        // Submesh's pushConstants.
        fx->setTransformIndex(mesh->transform_index);
        fx->setMaterialIndex(submesh->material_ref->material_index);
        fx->setInstanceIndex(instance_index++); //
        fx->pushConstant(pass);

        pass.set_primitive_topology(mesh->vk_primitive_topology());
        pass.draw(submesh->draw_descriptor, vertex_buffer, index_buffer); //
      }
    }
  }
}

// ----------------------------------------------------------------------------

void GPUResources::set_ray_tracing_fx(RayTracingFx* fx) {
  LOG_CHECK(fx != nullptr);
  fx->buildMaterialStorageBuffer(material_proxies); //
  ray_tracing_fx_ = fx;
}

// ----------------------------------------------------------------------------

void GPUResources::upload_images(Context const& context) {
  LOG_CHECK( total_image_size > 0 );

  /* Create a staging buffer. */
  backend::Buffer staging_buffer{
    context.create_staging_buffer( total_image_size ) //
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
    device_images.push_back(context.create_image_2d(
      extent.width,
      extent.height,
      VK_FORMAT_R8G8B8A8_UNORM, //
        VK_IMAGE_USAGE_SAMPLED_BIT
      | VK_IMAGE_USAGE_TRANSFER_DST_BIT
    ));

    /* Upload image to staging buffer */
    auto const img_bytesize = host_image.getBytesize();
    context.write_buffer(
      staging_buffer, staging_offset, host_image.getPixels(), 0u, img_bytesize
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

  auto cmd{ context.create_transient_command_encoder(Context::TargetQueue::Transfer) };
  {
    VkImageLayout const transfer_layout{ VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL };

    cmd.transition_images_layout(
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
    cmd.transition_images_layout(
      device_images,
      transfer_layout,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      layer_count
    );
  }
  context.finish_transient_command_encoder(cmd);
}

// ----------------------------------------------------------------------------

void GPUResources::upload_buffers(Context const& context) {
  LOG_CHECK(vertex_buffer_size > 0);

  VkBufferUsageFlags extra_flags{};

  // ---------------------------------------
  if (rt_scene_) {
    extra_flags = extra_flags
                // Position & Indices are needed for the BLAS.
                | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
                // Attributes & Indices are fetched by the closeshit shaders.
                | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
                | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
                ;
  }
  // ---------------------------------------

  /* Allocate device buffers for meshes & their transforms. */
  vertex_buffer = context.create_buffer(
    vertex_buffer_size,
      VK_BUFFER_USAGE_2_VERTEX_BUFFER_BIT
    | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR
    | extra_flags
    ,
    VMA_MEMORY_USAGE_GPU_ONLY
  );

  if (index_buffer_size > 0) {
    index_buffer = context.create_buffer(
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
    // We assume most meshes would be static, so with unfrequent updates.
    transforms_ssbo_ = context.create_buffer(
      transforms_buffer_size,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_TRANSFER_DST_BIT
      ,
      VMA_MEMORY_USAGE_GPU_ONLY
    );
  }

  /* Copy host mesh data to the staging buffer. */
  auto staging_buffer = context.create_staging_buffer(
    vertex_buffer_size + index_buffer_size + transforms_buffer_size
  );
  {
    size_t vertex_offset{0lu};
    size_t index_offset{vertex_buffer_size};

    // Transfer the attributes & indices by ranges.
    std::byte* device_data{};
    context.map_memory(staging_buffer, (void**)&device_data);
    for (auto const& mesh : meshes) {
      auto const& vertices = mesh->get_vertices();
      memcpy(device_data + vertex_offset, vertices.data(), vertices.size());
      vertex_offset += vertices.size();

      if (index_buffer_size > 0) {
        auto const& indices = mesh->get_indices();
        memcpy(device_data + index_offset, indices.data(), indices.size());
        index_offset += indices.size();
      }
    }

    // Transfer the transforms buffer in one go.
    memcpy(
      device_data + vertex_buffer_size + index_buffer_size,
      transforms.data(),
      transforms_buffer_size
    );

    context.unmap_memory(staging_buffer);
  }

  /* Copy device data from staging buffers to their respective buffers. */
  auto cmd = context.create_transient_command_encoder(Context::TargetQueue::Transfer);
  {
    size_t src_offset{0lu};

    src_offset = cmd.copy_buffer(staging_buffer, src_offset, vertex_buffer, 0u, vertex_buffer_size);

    if (index_buffer_size > 0) {
      src_offset = cmd.copy_buffer(staging_buffer, src_offset, index_buffer, 0u, index_buffer_size);
    }

    src_offset = cmd.copy_buffer(staging_buffer, src_offset, transforms_ssbo_, 0u, transforms_buffer_size);

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
        .buffer = transforms_ssbo_.buffer,
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
    cmd.pipeline_buffer_barriers(barriers);
  }
  context.finish_transient_command_encoder(cmd);
}

// ----------------------------------------------------------------------------

void GPUResources::update_frame_data(
  Camera const& camera,
  VkExtent2D const& surfaceSize,
  float elapsedTime
) {
  FrameData frame_data{
    .projectionMatrix = camera.proj(),
    .invProjectionMatrix = camera.projInverse(),
    .viewMatrix = camera.view(),
    .invViewMatrix = camera.world(),
    .viewProjMatrix = camera.viewproj(),
    .cameraPos_Time = vec4(camera.position(), elapsedTime),
    .resolution = vec2(surfaceSize.width, surfaceSize.height),
    .frame = frame_index_++,
  };

  context_ptr_->transfer_host_to_device(
    &frame_data, sizeof(frame_data), frame_ubo_
  );
}

/* -------------------------------------------------------------------------- */
