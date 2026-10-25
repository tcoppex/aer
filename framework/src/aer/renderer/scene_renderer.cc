#include <algorithm>

#include "aer/renderer/scene_renderer.h"

#include "aer/platform/vulkan/command_encoder.h"
#include "aer/scene/camera.h"
#include "aer/renderer/render_context.h"
#include "aer/renderer/vertex_layout.h" // for material_shader_interop::FrameData
#include "aer/renderer/renderer_flags.h"

/* -------------------------------------------------------------------------- */

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

  // Transforms SBO (growable)
  return growTransforms(kInitialRenderableCapacity);
}

// ----------------------------------------------------------------------------

void SceneRenderer::release() {
  if (context_ptr_ == nullptr) {
    return;
  }
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
  uint32_t frame_index,
  RendererFlags renderer_flags
) {
  LOG_CHECK(frame_sbo_.valid());
  LOG_CHECK(transforms_sbo_.valid());
  LOG_CHECK(frame_index < frames_in_flight_);

  ring_slot_ = frame_index;
  collectRetired();

  buildDrawLists(scene, camera);

  if (transforms_staging_.size() > transforms_capacity_) {
    if (!growTransforms(static_cast<uint32_t>(transforms_staging_.size()))) {
      // skip the frame's draw lists rather than overflowing the buffer
      for (auto& lookup : lookups_) lookup.clear();
      return;
    }
  }

  updateFrameData(camera, elapsed_time, renderer_flags);
  uploadTransforms();

  ++frame_counter_;
}

// ----------------------------------------------------------------------------

void SceneRenderer::render(RenderPassEncoder const& pass) {
  if (!pool_ptr_ || frame_data_current_address_ == 0u) {
    return;
  }
  auto const& vertex_buffer = pool_ptr_->vertex_buffer();
  auto const& index_buffer  = pool_ptr_->index_buffer();

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
        fx->set_push_constant_generic({
          .frame_buffer_address     = frame_data_current_address_,
          .transform_buffer_address = transforms_current_address_,
          .material_buffer_address  = material_buffer_address,
          .transform_index          = item.transform_slot,
          .material_index           = item.material->material_index,
          .instance_index           = instance_index++,
        });
        fx->pushConstant(pass);

        pass.setPrimitiveTopology(item.mesh->topology);
        pass.setCullMode(item.material->double_sided ? VK_CULL_MODE_NONE
                                                     : VK_CULL_MODE_BACK_BIT);

        pass.bindAndDraw(item.submesh->draw_descriptor, vertex_buffer, index_buffer);
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

void SceneRenderer::buildDrawLists(scene::Hierarchy& scene, Camera const& camera) {
  // LOG_CHECK(!ray_tracing_fx_ || !ray_tracing_fx_->is_enable());

  using namespace scene::component;

  for (auto& lookup : lookups_) {
    lookup.clear();
  }
  transforms_staging_.clear();

  vec3 const camera_pos = camera.position();
  vec3 const camera_dir = camera.direction();

  // -- Retrieve submeshes associated to each MaterialFx --

  scene.registry.view<GlobalTransform, Renderable>().each(
    [&](GlobalTransform const& global, Renderable const& renderable) {
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

      for (auto const& submesh : mesh.submeshes) {
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

void SceneRenderer::updateFrameData(
  Camera const& camera, float elapsed_time, RendererFlags renderer_flags
) {
  // Current surface size, provided by the Renderer to the RenderContext.
  auto const& surface_size = context_ptr_->default_surface_size(); //

  auto frame_data = material_shader_interop::FrameData{
    .default_world_matrix = context_ptr_->default_world_matrix(),
    .cameraPos_Time = vec4(camera.position(), elapsed_time),
    .resolution = vec2(surface_size.width, surface_size.height),
    .frame = static_cast<uint32_t>(frame_counter_),
    .renderer_states = underlying_cast(renderer_flags),
  };

  /* Copy the multiview camera transforms (bounded by the destination size). */
  {
    static_assert(std::is_trivially_copyable_v<Camera::Transform>);
    auto const& src = camera.transforms();
    auto& dst = frame_data.cameras;
    size_t const count = std::min<size_t>(src.size(), std::size(dst));
    std::memcpy(dst, (void const*)src.data(), sizeof(Camera::Transform) * count);
  }

  VkDeviceSize const offset = VkDeviceSize(ring_slot_) * frame_data_stride_;
  context_ptr_->writeBuffer(frame_sbo_, offset, &frame_data, 0u, sizeof(frame_data));

  frame_data_current_address_ = frame_sbo_.address + offset;
}

/* -------------------------------------------------------------------------- */