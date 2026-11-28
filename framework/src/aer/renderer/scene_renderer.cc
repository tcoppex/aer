#include <cstring>
#include <algorithm>

#include "aer/renderer/scene_renderer.h"

#include "aer/platform/vulkan/command_encoder.h"
#include "aer/scene/camera.h"
#include "aer/scene/skin_vertex.h"              // for material_shader_interop::SkinVertex
#include "aer/scene/ecs/animation_components.h" // for component::Skin
#include "aer/renderer/render_context.h"
#include "aer/renderer/vertex_layout.h"         // for material_shader_interop::FrameData
#include "aer/renderer/renderer_flags.h"
#include "aer/renderer/blas_builder.h"          // for accel::Destroy

/* -------------------------------------------------------------------------- */

namespace {

// Vulkan expects a row-major 3x4 matrix, lina is column-major.
void ToVkTransformMatrix(mat4 const& m, VkTransformMatrixKHR& out) {
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 4; ++col) {
      out.matrix[row][col] = static_cast<float>(m[col][row]);
    }
  }
}

}

/* -------------------------------------------------------------------------- */

SceneRenderer::SceneRenderer() = default;
SceneRenderer::~SceneRenderer() = default;

// ----------------------------------------------------------------------------

bool SceneRenderer::setup(
  RenderContext const& ctx,
  GpuPool& gpu_pool,
  MaterialFxRegistry& material_fx_registry,
  uint32_t frames_in_flight
) {
  LOG_CHECK(frames_in_flight > 0u);

  context_ptr_ = &ctx;
  pool_ptr_ = &gpu_pool;
  frames_in_flight_ = frames_in_flight;

  /* Create the Frame SBO fixed ring. */
  {
    auto align = context_ptr_->gpu_properties().limits.minUniformBufferOffsetAlignment;

    frame_data_stride_ = utils::AlignTo(
      sizeof(material_shader_interop::FrameData),
      align
    );

    frame_sbo_ = context_ptr_->createBuffer(
      frame_data_stride_ * frames_in_flight_,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
      ,
      VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
  }
  if (!frame_sbo_.valid()) {
    LOGE("SceneRenderer: failed to allocated the frame ring buffer.");
    return false;
  }

  if (!setupSkinningPipeline()) {
    return false;
  }

  // Transforms SBO (growable)
  return growTransforms(kInitialRenderableCapacity);
}

// ----------------------------------------------------------------------------

void SceneRenderer::release() {
  if (context_ptr_ == nullptr) {
    return;
  }

  releaseRayTracing();

  if (skinning_layout_ != VK_NULL_HANDLE) {
    context_ptr_->destroyPipeline(skinning_pipeline_);
    context_ptr_->destroyPipelineLayout(skinning_layout_);
  }
  context_ptr_->destroyBuffer(palette_.buffer);
  context_ptr_->destroyBuffer(skinned_.buffer);

  context_ptr_->destroyBuffer(frame_sbo_);
  context_ptr_->destroyBuffer(transforms_sbo_);
  for (auto const& r : retired_buffers_) {
    context_ptr_->destroyBuffer(r.buffer);
  }

  *this = {};
}

// ----------------------------------------------------------------------------

void SceneRenderer::update(
  scene::Hierarchy& scene,
  Camera const& camera,
  float elapsed_time,
  RendererFlags flags
) {
  LOG_CHECK(frame_sbo_.valid());
  LOG_CHECK(transforms_sbo_.valid());

  buildSkinning(scene);
  buildDrawLists(scene, camera);
  buildFrameData(camera, elapsed_time, flags);
  collectRayTracingInstances(scene);

  pending_upload_ = true;
}

// ----------------------------------------------------------------------------

void SceneRenderer::upload(uint32_t frame_index) {
  LOG_CHECK(frame_index < frames_in_flight_);
  ring_slot_ = frame_index;

  if (!pending_upload_) {
    return;
  }
  pending_upload_ = false;
  collectRetired();

  if (transforms_staging_.size() > transforms_capacity_
   && !growTransforms(static_cast<uint32_t>(transforms_staging_.size()))) {
    for (auto& lookup : lookups_) {
      lookup.clear();
    }
    return;
  }

  writeFrameData();
  uploadTransforms();
  uploadSkinning();

  ++frame_counter_;
}

// ----------------------------------------------------------------------------

void SceneRenderer::render(RenderPassEncoder const& pass) {
  if (!pool_ptr_ || frame_data_current_address_ == 0u) {
    return;
  }
  auto const& vertex_buffer = pool_ptr_->vertex_buffer();
  auto const& index_buffer  = pool_ptr_->index_buffer();

  // Skinned submeshes read the current slice of the skinned buffer.
  bool const skinned_ready = (skinned_.current_address != 0u);
  VkDeviceSize const skinned_slice = static_cast<VkDeviceSize>(ring_slot_) * skinned_.stride;

  uint32_t instance_index = 0u;

  for (auto& lookup : lookups_) {
    for (auto& [key, items] : lookup) {
      if (items.empty()) {
        continue;
      }
      auto const [fx, states] = key;

      fx->prepareDrawState(pass, states);
      auto const material_buffer_address = fx->material_buffer_address();

      for (auto const& item : items) {
        auto const& mat = item.material;
        fx->set_push_constant_generic({
          .frame_buffer_address     = frame_data_current_address_,
          .transform_buffer_address = transforms_current_address_,
          .material_buffer_address  = material_buffer_address,
          .transform_index          = item.transform_slot,
          .material_index           = mat->material_index,
          .instance_index           = instance_index++,
        });
        fx->pushConstant(pass);

        pass.setPrimitiveTopology(item.mesh->topology);
        pass.setCullMode(mat->double_sided ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);

        auto const& desc = item.submesh->draw_descriptor;
        if (skinned_ready && item.skinned_offset != kInvalidIndexU64) {
          auto vertex_base = skinned_slice + item.skinned_offset;
          pass.bindAndDraw(desc, skinned_.buffer, index_buffer, vertex_base);
        } else {
          pass.bindAndDraw(desc, vertex_buffer, index_buffer);
        }
      }
    }
  }
}

/* -------------------------------------------------------------------------- */

bool SceneRenderer::growTransforms(uint32_t min_count) {
  uint32_t capacity = std::max(transforms_capacity_, kInitialRenderableCapacity);
  while (capacity < min_count) {
    capacity *= 2u;
  }
  if (capacity == transforms_capacity_) {
    return true;
  }

  VkDeviceSize const slice_stride = utils::AlignTo(
    static_cast<VkDeviceSize>(capacity) * sizeof(mat4),
    kSliceAlignment
  );

  auto new_sbo = context_ptr_->createBuffer(
    slice_stride * frames_in_flight_,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ,
    VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
  );

  if (!new_sbo.valid()) {
    LOGE("SceneRenderer: failed to grow the transforms buffer ({} matrices).", capacity);
    return false;
  }

  if (transforms_sbo_.valid()) {
    retired_buffers_.push_back({
      .buffer = transforms_sbo_,
      .frame = frame_counter_ + frames_in_flight_ // (safe to free after this frame)
    });
  }

  transforms_sbo_           = new_sbo;
  transforms_capacity_      = capacity;
  transforms_slice_stride_  = slice_stride;
  transforms_staging_.reserve(capacity);

  return true;
}

// ----------------------------------------------------------------------------

void SceneRenderer::uploadTransforms() {
  if (transforms_staging_.empty()) {
    return;
  }
  LOG_CHECK(transforms_staging_.size() <= transforms_capacity_);

  // uint32_t const slot = static_cast<uint32_t>(frame_counter_ % frames_in_flight_);
  // VkDeviceSize const offset = slot * transforms_slice_stride_;

  // (we do not use internal 'frame_counter_' in case the renderer kept the same frame).
  VkDeviceSize const offset = static_cast<VkDeviceSize>(ring_slot_) * transforms_slice_stride_;

  context_ptr_->writeBuffer(
    transforms_sbo_,
    offset,
    transforms_staging_.data(),
    0u,
    transforms_staging_.size() * sizeof(mat4)
  );

  transforms_current_address_ = transforms_sbo_.address + offset;
}

// ----------------------------------------------------------------------------

void SceneRenderer::collectRetired() {
  std::erase_if(retired_buffers_, [this](RetiredBuffer const& r) {
    if (frame_counter_ < r.frame) {
      return false;
    }
    context_ptr_->destroyBuffer(r.buffer);
    return true;
  });
}

// ----------------------------------------------------------------------------

bool SceneRenderer::ensureSliceBuffer(
  SliceBuffer& slices,
  VkDeviceSize min_slice_bytes,
  VkBufferUsageFlags2KHR usage,
  VmaMemoryUsage memory_usage,
  VmaAllocationCreateFlags flags
) {
  if (min_slice_bytes <= slices.stride) {
    return true;
  }

  VkDeviceSize stride = std::max<VkDeviceSize>(slices.stride, kSliceAlignment);
  while (stride < min_slice_bytes) {
    stride *= 2u;
  }

  auto buffer = context_ptr_->createBuffer(
    stride * frames_in_flight_, usage, memory_usage, flags
  );
  if (!buffer.valid()) {
    LOGE("SceneRenderer: failed to create a skinning buffer.");
    return false;
  }

  // Contents are rewritten every frame, the old buffer only needs to outlive
  // the frames in flight.
  if (slices.buffer.valid()) {
    retired_buffers_.push_back({
      .buffer = slices.buffer,
      .frame = frame_counter_ + frames_in_flight_
    });
  }
  slices.buffer = buffer;
  slices.stride = stride;
  return true;
}

// ----------------------------------------------------------------------------

void SceneRenderer::buildSkinning(scene::Hierarchy& scene) {
  skinning_jobs_.clear();
  skinned_first_job_.clear();
  palette_staging_.clear();
  skinned_bytes_ = 0u;

  if (!raster_enabled_ && !rt_.enabled) {
    return;
  }

  using namespace scene::component;

  // static_assert(
  //   static_cast<uint32_t>(SkinningMode::Linear) == skinning_shader_interop::kSkinningModeLinear
  //   && static_cast<uint32_t>(SkinningMode::DualQuaternion) == skinning_shader_interop::kSkinningModeDualQuaternion,
  //   "SkinningMode must match the shader interop constants."
  // );

  auto& reg = scene.registry;

  reg.view<Renderable>().each(
    [&](entt::entity entity, Renderable const& renderable) {
      auto const* skin  = reg.try_get<Skin>(entity);
      auto const* morph = reg.try_get<MorphWeights>(entity);
      bool const use_skin  = skin  && !skin->matrices.empty();
      bool const use_morph = morph && !morph->weights.empty();
      if (!use_skin && !use_morph) {
        return;
      }

      // retrieve the device asset associated with the mesh.
      auto const* da = pool_ptr_->find(renderable.asset);
      if (!da || renderable.mesh_index >= da->meshes.size()) {
        return;
      }
      if (use_skin && skin->matrices.size() > kJointsWarnThreshold) {
        HLOGW("SceneRenderer: a skin has {} joints (> {}).", skin->matrices.size(), kJointsWarnThreshold);
      }
      auto const& mesh = da->meshes[renderable.mesh_index];

      // Shared by the submeshes of the entity, in mat4 slots of palette_staging_.
      uint32_t palette_offset = kInvalidIndexU32;
      uint32_t weights_offset = kInvalidIndexU32;

      for (auto const& submesh : mesh.submeshes) {
        auto const& desc = submesh.draw_descriptor;
        // (must stay the same predicate as the draw list and BLAS traversals)
        if (!submesh.is_deformable() || desc.vertexCount == 0u) {
          continue;
        }

        bool const skin_on  = use_skin  && submesh.has_skin();
        bool const morph_on = use_morph && submesh.has_morph();

        if (skin_on && palette_offset == kInvalidIndexU32) {
          palette_offset = static_cast<uint32_t>(palette_staging_.size());
          palette_staging_.insert(
            palette_staging_.end(), skin->matrices.begin(), skin->matrices.end()
          );
        }
        if (morph_on && weights_offset == kInvalidIndexU32) {
          weights_offset = static_cast<uint32_t>(palette_staging_.size());
          size_t const slots = (morph->weights.size() + 15u) / 16u;  // 16 floats per mat4
          palette_staging_.resize(palette_staging_.size() + slots, mat4{});
          std::memcpy(
            reinterpret_cast<float*>(palette_staging_.data() + weights_offset), //
            morph->weights.data(),
            morph->weights.size() * sizeof(float)
          );
        }

        skinned_first_job_.try_emplace(entity, static_cast<uint32_t>(skinning_jobs_.size()));

        skinned_bytes_ = utils::AlignTo(skinned_bytes_, kSkinnedAlignment);
        skinning_jobs_.push_back({
          .vertex_offset  = desc.vertexOffset,
          .skin_offset    = skin_on  ? submesh.skin_vertex_offset : kInvalidIndexU64,
          .morph_offset   = morph_on ? submesh.morph_offset       : kInvalidIndexU64,
          .out_offset     = skinned_bytes_,
          .palette_offset = skin_on  ? palette_offset : 0u,
          .weights_offset = morph_on ? weights_offset : 0u,
          .joint_count    = skin_on  ? static_cast<uint32_t>(skin->matrices.size()) : 0u,
          .vertex_count   = desc.vertexCount,
          .morph_count    = morph_on
            ? std::min(submesh.morph_count, static_cast<uint32_t>(morph->weights.size()))
            : 0u,
          .mode           = skin_on
            ? static_cast<uint32_t>(skin->palette_mode)
            : skinning_shader_interop::kSkinningModeNone,
        });
        skinned_bytes_ += static_cast<uint64_t>(desc.vertexCount)
                        * sizeof(material_shader_interop::Vertex);
      }
    }
  );

  // A job may use neither palette nor weights, keep the buffer valid anyway.
  if (!skinning_jobs_.empty() && palette_staging_.empty()) {
    palette_staging_.push_back(mat4{lina::identity});
  }
}

// ----------------------------------------------------------------------------

void SceneRenderer::uploadSkinning() {
  skinning_pending_ = false;
  palette_.current_address = 0u;
  skinned_.current_address = 0u;

  if (skinning_jobs_.empty()) {
    return;
  }

  VkBufferUsageFlags2KHR skinned_usage =
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    ;

  if (pool_ptr_->settings().ray_tracing) {
    skinned_usage |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
  }

  auto const palette_bytesize = palette_staging_.size() * sizeof(palette_staging_[0]);

  bool const ok = ensureSliceBuffer(
    palette_,
    palette_bytesize,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
    | VMA_ALLOCATION_CREATE_MAPPED_BIT
  ) && ensureSliceBuffer(
    skinned_,
    skinned_bytes_,
    skinned_usage,
    VMA_MEMORY_USAGE_GPU_ONLY,
    {}
  );

  if (!ok) {
    skinning_jobs_.clear();
    return;
  }

  VkDeviceSize const palette_offset = VkDeviceSize(ring_slot_) * palette_.stride;
  VkDeviceSize const skinned_offset = VkDeviceSize(ring_slot_) * skinned_.stride;

  context_ptr_->writeBuffer(
    palette_.buffer,
    palette_offset,
    palette_staging_.data(),
    0u,
    palette_bytesize
  );
  palette_.current_address = palette_.buffer.address + palette_offset;
  skinned_.current_address = skinned_.buffer.address + skinned_offset;

  skinning_pending_ = skinning_layout_ != VK_NULL_HANDLE;
}

// ----------------------------------------------------------------------------

bool SceneRenderer::setupSkinningPipeline() {
  skinning_layout_ = context_ptr_->createPipelineLayout({
    .pushConstantRanges = {
      {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(skinning_shader_interop::SkinningPushConstant),
      }
    }
  });
  if (skinning_layout_ == VK_NULL_HANDLE) {
    return false;
  }

  auto const shader = context_ptr_->createShaderModule(
    FRAMEWORK_COMPILED_SHADERS_DIR "skinning/", "skin_vertices.slang"
  );
  skinning_pipeline_ = context_ptr_->createComputePipeline(
    skinning_layout_,
    shader
  );
  context_ptr_->releaseShaderModule(shader);

  return true;
}

// ----------------------------------------------------------------------------

void SceneRenderer::computeSkinning(CommandEncoder const& cmd) {
  if (!skinning_pending_ || skinning_jobs_.empty()) {
    return;
  }
  skinning_pending_ = false;

  LOG_CHECK(palette_.current_address != 0u && skinned_.current_address != 0u);

  auto const vertex_base = pool_ptr_->vertex_buffer().address;
  cmd.bindPipeline(skinning_pipeline_);

  for (auto const& job : skinning_jobs_) {
    constexpr uint64_t kSlotBytes = sizeof(decltype(palette_staging_)::value_type);
    auto const pc = skinning_shader_interop::SkinningPushConstant{
      .src_vertices  = vertex_base + job.vertex_offset,
      .skin_vertices = (job.skin_offset != kInvalidIndexU64) ? vertex_base + job.skin_offset : 0u,
      .dst_vertices  = skinned_.current_address + job.out_offset,
      .palette       = palette_.current_address + job.palette_offset * kSlotBytes,
      .morph_deltas  = (job.morph_offset != kInvalidIndexU64) ? vertex_base + job.morph_offset : 0u,
      .morph_weights = palette_.current_address + job.weights_offset * kSlotBytes,
      .vertex_count  = job.vertex_count,
      .joint_count   = job.joint_count,
      .mode          = job.mode,
      .morph_count   = job.morph_count,
    };
    cmd.pushConstant(pc, VK_SHADER_STAGE_COMPUTE_BIT);
    cmd.runKernel<skinning_shader_interop::kSkinningGroupSize>(job.vertex_count);
  }

  VkPipelineStageFlags2 dst_stages  = VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT;
  VkAccessFlags2        dst_access  = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
  if (rt_.enabled) {
    dst_stages |= VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR
               |  VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
               |  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT
               |  VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    dst_access |= VK_ACCESS_2_SHADER_READ_BIT;
  }

  cmd.pipelineBufferBarriers({{
    .srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
    .srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
    .dstStageMask  = dst_stages,
    .dstAccessMask = dst_access,
    .buffer = skinned_.buffer.buffer,
    .offset = VkDeviceSize(ring_slot_) * skinned_.stride,
    .size   = skinned_bytes_,
  }});
}

// ----------------------------------------------------------------------------

void SceneRenderer::buildDrawLists(scene::Hierarchy& scene, Camera const& camera) {
  for (auto& lookup : lookups_) {
    lookup.clear();
  }
  transforms_staging_.clear();

  if (!raster_enabled_) {
    return;
  }

  vec3 const camera_pos = camera.position();
  vec3 const camera_dir = camera.direction();

  // -- Retrieve submeshes associated to each MaterialFx --
  using namespace scene::component;

  scene.registry.view<GlobalTransform, Renderable>().each(
    [&](entt::entity entity, GlobalTransform const& global, Renderable const& renderable) {
      // Resolve the asset, skip stale handles.
      auto const* da = pool_ptr_->find(renderable.asset);
      if (!da || renderable.mesh_index >= da->meshes.size()) {
        return;
      }
      auto const& mesh = da->meshes[renderable.mesh_index];

      uint32_t const slot = static_cast<uint32_t>(transforms_staging_.size());
      transforms_staging_.push_back(global.worldMatrix);

      vec3 const pos = lina::to_vec3(global.worldMatrix.w);
      float const sort_key = lina::dot(camera_dir, camera_pos - pos);

      // Skinned submeshes of this entity map to consecutive jobs.
      uint32_t next_job = kInvalidIndexU32;
      if (auto const it = skinned_first_job_.find(entity); it != skinned_first_job_.end()) {
        next_job = it->second;
      }

      for (auto const& submesh : mesh.submeshes) {
        uint64_t skinned_offset = kInvalidIndexU64;
        if ((next_job != kInvalidIndexU32)
          && submesh.is_deformable()
          && submesh.draw_descriptor.vertexCount > 0u) {
          skinned_offset = skinning_jobs_[next_job++].out_offset;
        }
        if (submesh.asset_material_index >= da->materials.size()) {
          continue;
        }
        auto const& material = da->materials[submesh.asset_material_index];
        if (!material.fx) {
          continue;
        }
        lookups_[material.states.alpha_mode][FxHashPair{material.fx, material.states}]
          .push_back({
            .submesh = &submesh,
            .material = &material,
            .mesh = &mesh,
            .transform_slot = slot,
            .skinned_offset = skinned_offset,
            .sort_key = sort_key,
          });
      }
    }
  );

  // -- Sort each buffer of submeshes --

  // Stable sort keeps the submesh order of a same renderable (identical keys).
  auto sort_buckets = [](auto& lookup, auto comp) {
    for (auto& [_, items] : lookup) {
      std::ranges::stable_sort(items, comp, &DrawItem::sort_key);
    }
  };

  sort_buckets(lookups_[AlphaMode::Opaque], std::less{});     // front to back (early-z)
  // sort_buckets(lookups_[AlphaMode::Mask],   std::less{});     // mask stays unsorted
  sort_buckets(lookups_[AlphaMode::Blend],  std::greater{});  // back to front
}

// ----------------------------------------------------------------------------

void SceneRenderer::set_ray_tracing_enabled(bool enabled) {
  rt_.enabled = enabled;
  if (!enabled) {
    rt_.instances.clear();
    rt_.geometries.clear();
    rt_.prev_instances.clear();
    rt_.prev_geometries.clear();
    rt_.dynamic_instances.clear();
    releaseRayTracing();
  }
  ++rt_.version;
}

// ----------------------------------------------------------------------------

void SceneRenderer::collectRayTracingInstances(scene::Hierarchy& scene) {
  if (!rt_.enabled) {
    return;
  }
  using namespace scene::component;

  // Keep last frame's content to detect changes, reuse its storage.
  std::swap(rt_.instances,  rt_.prev_instances);
  std::swap(rt_.geometries, rt_.prev_geometries);
  rt_.instances.clear();
  rt_.geometries.clear();
  rt_.dynamic_instances.clear();

  auto const vertex_base = pool_ptr_->vertex_buffer().address;
  auto const index_base  = pool_ptr_->index_buffer().address;

  scene.registry.view<GlobalTransform, Renderable>().each(
    [&](entt::entity entity, GlobalTransform const& global, Renderable const& renderable) {
      auto const* da = pool_ptr_->find(renderable.asset);
      if (!da || (renderable.mesh_index >= da->meshes.size())) {
        return;
      }
      auto const& mesh = da->meshes[renderable.mesh_index];
      if (!mesh.has_blas()) {
        return;
      }

      // instanceCustomIndex is 24 bits.
      auto const first = static_cast<uint32_t>(rt_.geometries.size());
      if (first + mesh.submeshes.size() > 0x00FFFFFFu) {
        return;
      }

      for (auto const& submesh : mesh.submeshes) {
        auto const& desc = submesh.draw_descriptor;

        DeviceMaterial const* material = (submesh.asset_material_index < da->materials.size())
          ? &da->materials[submesh.asset_material_index]
          : nullptr;
        bool const has_material = material
                               && material->fx
                               // && material->fx->ray_tracing_compatible()
                               ;

        auto record = material_shader_interop::RTGeometryData{
          .vertex_address   = vertex_base + desc.vertexOffset,
          .index_address    = index_base + desc.indexOffset,
          .material_address = has_material ? material->fx->material_buffer_address() : 0u,
          .material_index   = has_material ? material->material_index : 0u,
          .index_is_u16     = (desc.indexType == VK_INDEX_TYPE_UINT16) ? 1u : 0u,
        };
        rt_.geometries.push_back(record);
      }

      auto instance = VkAccelerationStructureInstanceKHR{
        .instanceCustomIndex = first,
        .mask = 0xFF,
        .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = mesh.blas.address,
      };
      ToVkTransformMatrix(global.worldMatrix, instance.transform);

      auto const instance_index = static_cast<uint32_t>(rt_.instances.size());
      rt_.instances.push_back(instance);

      // Add skinned entities to the dynamic instances.
      if (auto const it = skinned_first_job_.find(entity); it != skinned_first_job_.end()) {
        rt_.dynamic_instances.push_back({
          .entity = entity,
          .asset = da,
          .mesh = &mesh,
          .instance_index = instance_index,
          .first_geometry = first,
          .first_job = it->second,
        });
      }
    }
  );

  // ---------------------
  // Bump the version when anything changed.
  auto same = [](auto const& a, auto const& b) {
    return (a.size() == b.size())
        && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
  };
  if (!same(rt_.instances, rt_.prev_instances)
   || !same(rt_.geometries, rt_.prev_geometries)
   || !rt_.dynamic_instances.empty()) {
    ++rt_.version;
  }
  // ---------------------
}

// ----------------------------------------------------------------------------

void SceneRenderer::buildFrameData(
  Camera const& camera,
  float elapsed_time,
  RendererFlags renderer_flags
) {
  // Current surface size, provided by the Renderer to the RenderContext.
  auto const& surface_size = context_ptr_->default_surface_size(); //

  frame_data_ = material_shader_interop::FrameData{
    .default_world_matrix = context_ptr_->default_world_matrix(),
    .cameraPos_Time       = vec4(camera.position(), elapsed_time),
    .resolution           = vec2(surface_size.width, surface_size.height),
    .frame                = static_cast<uint32_t>(frame_counter_),
    .renderer_states      = underlying_cast(renderer_flags),
    .exposure             = exposure_,
    .tonemapping          = tonemapping_,
  };

  /* Copy the multiview camera transforms (bounded by the destination size). */
  {
    static_assert(std::is_trivially_copyable_v<Camera::Transform>);
    auto const& src = camera.transforms();
    auto& dst = frame_data_.cameras;
    size_t const count = std::min<size_t>(src.size(), std::size(dst));
    std::memcpy(dst, (void const*)src.data(), sizeof(Camera::Transform) * count);
  }
}

// ----------------------------------------------------------------------------

void SceneRenderer::writeFrameData() {
  VkDeviceSize const offset = VkDeviceSize(ring_slot_) * frame_data_stride_;
  context_ptr_->writeBuffer(frame_sbo_, offset, &frame_data_, 0u, sizeof(frame_data_));
  frame_data_current_address_ = frame_sbo_.address + offset;
}

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------

void SceneRenderer::releaseRayTracing() {
  for (auto& f : rt_.frames) {
    accel::Destroy(*context_ptr_, f.tlas);
    for (auto& [_, s] : f.dynamic.blas) {
      accel::Destroy(*context_ptr_, s.blas);
    }
    f.dynamic.blas.clear();
    for (auto* b : {
      &f.tlas_scratch,
      &f.instances,
      &f.geometry_table,
      &f.dynamic.scratch
    }) {
      if (b->valid()) {
        context_ptr_->destroyBuffer(*b);
      }
    }
  }
  rt_.frames.clear();
}

// ----------------------------------------------------------------------------

bool SceneRenderer::ensureRayTracingFrame(RayTracingFrame& f) {
  auto grow = [](size_t needed, uint32_t current) {
    uint32_t capacity = std::max(current, 64u);
    while (capacity < needed) {
      capacity *= 2u;
    }
    return capacity;
  };
  auto destroy = [&](backend::Buffer& b) {
    if (b.valid()) {
      context_ptr_->destroyBuffer(b);
    }
    b = {};
  };

  uint32_t const instance_capacity = grow(rt_.instances.size(),  f.instance_capacity);
  uint32_t const geometry_capacity = grow(rt_.geometries.size(), f.geometry_capacity);

  // -- Geometry table --
  if (geometry_capacity != f.geometry_capacity) {
    destroy(f.geometry_table);
    f.geometry_table = context_ptr_->createBuffer(
      "SceneRenderer::RT::GeometryTable",
      geometry_capacity * sizeof(material_shader_interop::RTGeometryData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
      | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
    if (!f.geometry_table.valid()) {
      f.geometry_capacity = 0u;
      return false;
    }
    f.geometry_capacity = geometry_capacity;
    f.built_version = kNoVersion;
  }

  // -- Instances + TLAS storage + scratch --
  if (instance_capacity != f.instance_capacity) {
    accel::Destroy(*context_ptr_, f.tlas);
    destroy(f.tlas_scratch);
    destroy(f.instances);
    f.instance_capacity = 0u;

    f.instances = context_ptr_->createBuffer(
      "SceneRenderer::RT::Instances",
      instance_capacity * sizeof(VkAccelerationStructureInstanceKHR),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_AUTO,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
      | VMA_ALLOCATION_CREATE_MAPPED_BIT
    );
    if (!f.instances.valid()) {
      return false;
    }

    // Size the TLAS for the capacity (the build range uses the actual count).
    VkAccelerationStructureGeometryKHR const geometry{
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
      .geometry = {
        .instances = {
          .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
          .arrayOfPointers = VK_FALSE,
          .data = { .deviceAddress = f.instances.address },
        }
      },
    };
    VkAccelerationStructureBuildGeometryInfoKHR const sizing_info{
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
      .mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = 1u,
      .pGeometries = &geometry,
    };
    VkAccelerationStructureBuildSizesInfoKHR sizes{
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    vkGetAccelerationStructureBuildSizesKHR(
      context_ptr_->device(),
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &sizing_info,
      &instance_capacity,
      &sizes
    );

    f.tlas.build_sizes_info = sizes;
    f.tlas.buffer = context_ptr_->createBuffer(
      "SceneRenderer::RT::TLAS",
      sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY
    );
    f.tlas_scratch = context_ptr_->createBuffer(
      "SceneRenderer::RT::TLAS::Scratch",
      sizes.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    );
    if (!f.tlas.buffer.valid() || !f.tlas_scratch.valid()) {
      accel::Destroy(*context_ptr_, f.tlas);
      destroy(f.tlas_scratch);
      destroy(f.instances);
      return false;
    }

    VkAccelerationStructureCreateInfoKHR const create_info{
      .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = f.tlas.buffer.buffer,
      .size   = sizes.accelerationStructureSize,
      .type   = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    };
    CHECK_VK(vkCreateAccelerationStructureKHR(
      context_ptr_->device(), &create_info, nullptr, &f.tlas.handle
    ));

    f.instance_capacity = instance_capacity;
    f.built_version = kNoVersion;
  }
  return true;
}

// ----------------------------------------------------------------------------

void SceneRenderer::recordAccelerationStructures(CommandEncoder const& cmd) {
  if (!rt_.enabled || rt_.instances.empty()) {
    return;
  }
  if (rt_.frames.size() != frames_in_flight_) {
    rt_.frames.resize(frames_in_flight_);
  }
  LOG_CHECK(ring_slot_ < rt_.frames.size());
  auto& f = rt_.frames[ring_slot_];

  // this slot's TLAS is already up to date
  if (f.built_version == rt_.version) {
    return;
  }
  if (!ensureRayTracingFrame(f)) {
    LOGE("SceneRenderer: failed to allocate ray tracing resources.");
    return;
  }

  // Patches RT instances & geometries before they are written.
  recordDynamicBLAS(cmd, f);

  context_ptr_->writeBuffer(
    f.instances, 0u,
    rt_.instances.data(), 0u,
    rt_.instances.size() * sizeof(rt_.instances[0])
  );
  context_ptr_->writeBuffer(
    f.geometry_table, 0u,
    rt_.geometries.data(), 0u,
    rt_.geometries.size() * sizeof(rt_.geometries[0])
  );

  VkAccelerationStructureGeometryKHR const geometry{
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
    .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
    .geometry = {
      .instances = {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .arrayOfPointers = VK_FALSE,
        .data = { .deviceAddress = f.instances.address },
      }
    },
  };
  VkAccelerationStructureBuildGeometryInfoKHR const build_info{
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type  = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
    .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
    .mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    .dstAccelerationStructure = f.tlas.handle,
    .geometryCount = 1u,
    .pGeometries = &geometry,
    .scratchData = { .deviceAddress = f.tlas_scratch.address },
  };
  f.instance_count = static_cast<uint32_t>(rt_.instances.size());
  VkAccelerationStructureBuildRangeInfoKHR const range{
    .primitiveCount = f.instance_count
  };
  auto const* range_ptr = &range;

  vkCmdBuildAccelerationStructuresKHR(cmd.handle(), 1u, &build_info, &range_ptr);

  // TLAS build -> ray tracing shaders reading it.
  VkMemoryBarrier2 const barrier{
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
    .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
    .dstStageMask  = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR
                   | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,   // (hybrid raster + ray queries)
    .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
                   | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
  };
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .memoryBarrierCount = 1u,
    .pMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cmd.handle(), &dependency);

  f.built_version = rt_.version;
}
// ----------------------------------------------------------------------------

void SceneRenderer::recordDynamicBLAS(CommandEncoder const& cmd, RayTracingFrame& f) {
  auto const& vertex_buffer = pool_ptr_->vertex_buffer();
  auto const& index_buffer  = pool_ptr_->index_buffer();
  auto const& locations     = pool_ptr_->settings().attribute_locations;

  // (same condition as the raster path)
  bool const ready = (skinned_.current_address != 0u);

  auto& dynamic = f.dynamic;

  // Reset state to know which one to destroy afterwards.
  for (auto& [_, s] : dynamic.blas) {
    s.used = false;
  }

  // -- 1. Restore the static data, find or create the BLAS of each dynamic instance --

  std::vector<std::pair<RTDynamicInstance const*, RTDynamicBLAS*>> work{};
  VkDeviceSize scratch_needed = 0u;

  for (auto const& si : rt_.dynamic_instances) {
    auto const& mesh = *si.mesh;

    // Fallback state: shared bind pose BLAS and pool vertices.
    rt_.instances[si.instance_index].accelerationStructureReference = mesh.blas.address;
    for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
      rt_.geometries[si.first_geometry + i].vertex_address =
        vertex_buffer.address + mesh.submeshes[i].draw_descriptor.vertexOffset
      ;
    }
    if (!ready) {
      continue;
    }

    // When the mesh has changed (eg. new entity)
    auto& s = dynamic.blas[si.entity];
    if (s.mesh != si.mesh) {
      accel::Destroy(*context_ptr_, s.blas);
      s = {};
      if (!accel::CreateDynamicBLAS(
            *context_ptr_, *si.asset, mesh,
            vertex_buffer, index_buffer, locations, s.blas
      )) {
        dynamic.blas.erase(si.entity);
        continue;
      }
      s.mesh = si.mesh;
    }
    s.used = true;
    scratch_needed = std::max(scratch_needed, s.blas.scratch_size);
    work.emplace_back(&si, &s);
  }

  // Free the BLAS of gone entities since this slot's last use.
  std::erase_if(dynamic.blas, [&](auto& kv) {
    if (kv.second.used) {
      return false;
    }
    accel::Destroy(*context_ptr_, kv.second.blas);
    return true;
  });

  if (work.empty()) {
    return;
  }

  // -- 2. Scratch, shared by all the builds --

  if (scratch_needed > dynamic.scratch_size) {
    if (dynamic.scratch.valid()) {
      context_ptr_->destroyBuffer(dynamic.scratch);
    }
    dynamic.scratch = context_ptr_->createBuffer(
      "SceneRenderer::RT::DynamicBLAS::Scratch",
      scratch_needed,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VMA_MEMORY_USAGE_AUTO,
      VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
    );
    dynamic.scratch_size = dynamic.scratch.valid() ? scratch_needed : 0u;
    if (!dynamic.scratch.valid()) {
      LOGE("SceneRenderer: failed to allocate the dynamic BLAS scratch buffer.");
      return; // (instances stay on the bind pose BLAS)
    }
  }

  // -- 3. Build or refit each BLAS from this frame's skinned vertices, then patch --

  std::vector<VkDeviceAddress> addresses{};

  for (auto const& [si, s] : work) {
    auto const& mesh = *si->mesh;

    // Same traversal order as buildSkinning / buildDrawLists.
    addresses.resize(mesh.submeshes.size());
    uint32_t job = si->first_job;
    for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
      auto const& submesh = mesh.submeshes[i];
      if (submesh.is_deformable() && submesh.draw_descriptor.vertexCount > 0u) {
        LOG_CHECK(job < skinning_jobs_.size());
        addresses[i] = skinned_.current_address + skinning_jobs_[job++].out_offset;
      } else {
        addresses[i] = vertex_buffer.address + submesh.draw_descriptor.vertexOffset;
      }
    }

    bool const refit = s->built && (s->since_rebuild < kSkinnedBLASRebuildPeriod);

    if (!accel::BuildDynamicBLAS(
          cmd.handle(), *si->asset, mesh, vertex_buffer, index_buffer, locations,
          addresses, dynamic.scratch.address, s->blas, refit
    )) {
      s->built = false;
      continue; // (fallback to bind pose)
    }
    s->built = true;
    s->since_rebuild = refit ? (s->since_rebuild + 1u) : 0u;

    // The TLAS instance uses this BLAS, hit shaders read the skinned vertices.
    rt_.instances[si->instance_index].accelerationStructureReference = s->blas.address;
    for (size_t i = 0; i < addresses.size(); ++i) {
      rt_.geometries[si->first_geometry + i].vertex_address = addresses[i];
    }
  }
}

// ----------------------------------------------------------------------------

RayTracingView SceneRenderer::ray_tracing_view() const {
  if (!rt_.enabled || ring_slot_ >= rt_.frames.size()) {
    return {};
  }
  auto const& f = rt_.frames[ring_slot_];

  // Only expose a TLAS that was built for the current content, this frame.
  if ((f.tlas.handle == VK_NULL_HANDLE) || (f.built_version != rt_.version)) {
    return {};
  }
  return {
    .tlas = f.tlas.handle,
    .geometry_table_address = f.geometry_table.address,
    .frame_data_address = frame_data_current_address_,
    .generation = rt_.version,
  };
}

/* -------------------------------------------------------------------------- */