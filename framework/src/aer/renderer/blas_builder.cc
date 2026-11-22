#include "aer/renderer/blas_builder.h"

#include <algorithm>
#include <vector>

#include "aer/renderer/render_context.h"

/* -------------------------------------------------------------------------- */

namespace {

/* Build flags of the BLAS rebuilt every frame. */
constexpr VkBuildAccelerationStructureFlagsKHR kDynamicBuildFlags{
    VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR
  | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR
};

struct PendingBuild {
  DeviceMesh* mesh{};
  std::vector<VkAccelerationStructureGeometryKHR> geometries{};
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges{};
  VkAccelerationStructureBuildGeometryInfoKHR build_info{};
};

/* Position format & stride, read from the submesh vertex input. */
bool FindPositionLayout(
  DrawDescriptor const& desc,
  uint32_t position_location,
  VkFormat& out_format,
  uint32_t& out_stride
) {
  auto const& vi = desc.vertexInput;

  auto attr = std::ranges::find(
    vi.attributes, position_location,
    &VkVertexInputAttributeDescription2EXT::location
  );
  if (attr == vi.attributes.end()) {
    return false;
  }
  auto binding = std::ranges::find(
    vi.bindings, attr->binding,
    &VkVertexInputBindingDescription2EXT::binding
  );
  if (binding == vi.bindings.end()) {
    return false;
  }
  out_format = attr->format;
  out_stride = binding->stride;
  return true;
}

/**
 *  Build the triangle geometries of a mesh (one per submesh).
 * `vertex_addresses` (optional) overrides the first vertex address of each submesh.
 * Returns false when a submesh cannot be used for a BLAS.
 */
bool MakeGeometries(
  DeviceAsset const& asset,
  DeviceMesh const& mesh,
  uint32_t position_location,
  VkDeviceAddress vertex_base,
  VkDeviceAddress index_base,
  std::span<VkDeviceAddress const> vertex_addresses,
  std::vector<VkAccelerationStructureGeometryKHR>& geometries,
  std::vector<VkAccelerationStructureBuildRangeInfoKHR>& ranges,
  std::vector<uint32_t>& primitive_counts
) {
  geometries.clear();
  ranges.clear();
  primitive_counts.clear();

  if (!vertex_addresses.empty() && vertex_addresses.size() != mesh.submeshes.size()) {
    return false;
  }

  for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
    auto const& submesh = mesh.submeshes[i];
    auto const& desc = submesh.draw_descriptor;

    VkFormat format{};
    uint32_t stride{};
    if ((desc.indexType != VK_INDEX_TYPE_UINT16 && desc.indexType != VK_INDEX_TYPE_UINT32)
     || desc.indexCount < 3u
     || !FindPositionLayout(desc, position_location, format, stride)) {
      return false;
    }

    // Opaque geometry skips the any-hit shader.
    bool is_opaque = true;
    if (submesh.asset_material_index < asset.materials.size()) {
      auto const& states = asset.materials[submesh.asset_material_index].states;
      is_opaque = (states.alpha_mode == scene::MaterialStates::AlphaMode::Opaque);
    }

    VkDeviceAddress const vertex_address = vertex_addresses.empty()
      ? vertex_base + desc.vertexOffset
      : vertex_addresses[i];

    geometries.push_back({
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
      .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
      .geometry = {
        .triangles = {
          .sType         = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
          .vertexFormat  = format,
          .vertexData    = { .deviceAddress = vertex_address },
          .vertexStride  = stride,
          .maxVertex     = desc.vertexCount - 1u,
          .indexType     = desc.indexType,
          .indexData     = { .deviceAddress = index_base + desc.indexOffset },
        }
      },
      .flags = is_opaque ? VkGeometryFlagsKHR(VK_GEOMETRY_OPAQUE_BIT_KHR)
                         : VkGeometryFlagsKHR(0),
    });
    ranges.push_back({ .primitiveCount = desc.indexCount / 3u });
    primitive_counts.push_back(desc.indexCount / 3u);
  }
  return true;
}

} // namespace

/* -------------------------------------------------------------------------- */

namespace accel {

bool BuildBLAS(
  Context const& context,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer,
  Geometry::AttributeLocationMap const& attribute_locations,
  DeviceAsset& asset
) {
  auto const position_it = attribute_locations.find(Geometry::AttributeType::Position);
  if (position_it == attribute_locations.end()
   || vertex_buffer.address == 0u
   || index_buffer.address  == 0u) {
    LOGW("accel::BuildBLAS: needs device addresses and a position location.");
    return false;
  }

  bool all_built = true;
  std::vector<PendingBuild> pendings{};
  pendings.reserve(asset.meshes.size());
  VkDeviceSize max_scratch_size = 0u;

  // -- A. Setup the BLAS Geometry info --------------

  for (auto& mesh : asset.meshes) {
    if (mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST || mesh.submeshes.empty()) {
      continue;
    }

    PendingBuild pb{ .mesh = &mesh };
    std::vector<uint32_t> primitive_counts{};

    if (!MakeGeometries(
          asset, mesh, position_it->second,
          vertex_buffer.address, index_buffer.address, {},
          pb.geometries, pb.ranges, primitive_counts))
    {
      all_built = false;
      continue;
    }

    pb.build_info = {
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
      .type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
      .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
      .mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
      .geometryCount = static_cast<uint32_t>(pb.geometries.size()),
      .pGeometries   = pb.geometries.data(),
    };

    VkAccelerationStructureBuildSizesInfoKHR sizes{
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    vkGetAccelerationStructureBuildSizesKHR(
      context.device(),
      VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
      &pb.build_info,
      primitive_counts.data(),
      &sizes
    );

    auto& as = mesh.blas;
    as.build_sizes_info = sizes;
    as.buffer = context.createBuffer(
      "Pool::BLAS",
      sizes.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
      | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
    if (!as.buffer.valid()) {
      LOGE("accel::BuildBLAS: failed to allocate a BLAS buffer.");
      all_built = false;
      continue;
    }

    VkAccelerationStructureCreateInfoKHR const create_info{
      .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
      .buffer = as.buffer.buffer,
      .size   = sizes.accelerationStructureSize,
      .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    };
    CHECK_VK(vkCreateAccelerationStructureKHR(
      context.device(), &create_info, nullptr, &as.handle
    ));

    pb.build_info.dstAccelerationStructure = as.handle;
    max_scratch_size = std::max(max_scratch_size, sizes.buildScratchSize);
    pendings.push_back(std::move(pb)); // (moving a vector keeps its data pointer)
  }

  if (pendings.empty()) {
    return all_built;
  }

  // -- B. Create the acceleration structure via a scratch buffer --------------

  auto scratch = context.createBuffer(
    "Pool::BLAS::Scratch",
    max_scratch_size,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
    VMA_MEMORY_USAGE_AUTO,
    VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT
  );
  if (!scratch.valid()) {
    LOGE("accel::BuildBLAS: failed to allocate the scratch buffer.");
    for (auto& pb : pendings) {
      Destroy(context, pb.mesh->blas);
    }
    return false;
  }

  // AS builds need a compute capable queue, hence Main.
  auto const cmd = context.createTransientCommandEncoder(Context::TargetQueue::Main);
  {
    auto barrier = [&](VkMemoryBarrier2 const& b) {
      VkDependencyInfo const dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1u,
        .pMemoryBarriers = &b,
      };
      vkCmdPipelineBarrier2(cmd.handle(), &dep);
    };

    // Geometry copies -> build inputs.
    barrier({
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask  = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
      .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
    });

    for (auto& pb : pendings) {
      pb.build_info.scratchData.deviceAddress = scratch.address;
      auto const* range_ptr = pb.ranges.data();
      vkCmdBuildAccelerationStructuresKHR(cmd.handle(), 1u, &pb.build_info, &range_ptr);

      // Scratch reuse between builds + BLAS visible to later builds.
      barrier({
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
        .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
                       | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
      });
    }
  }
  context.finishTransientCommandEncoder(cmd); // (waits on a fence)
  context.destroyBuffer(scratch);

  // -- C. Retrieve Device addresses --------------

  for (auto& pb : pendings) {
    auto& as = pb.mesh->blas;
    VkAccelerationStructureDeviceAddressInfoKHR const info{
      .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
      .accelerationStructure = as.handle,
    };
    as.address = vkGetAccelerationStructureDeviceAddressKHR(context.device(), &info);
  }

  return all_built;
}

// ----------------------------------------------------------------------------

bool CreateDynamicBLAS(
  Context const& context,
  DeviceAsset const& asset,
  DeviceMesh const& mesh,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer,
  Geometry::AttributeLocationMap const& attribute_locations,
  backend::AccelerationStructure &out
) {
  out = {};

  auto const position_it = attribute_locations.find(Geometry::AttributeType::Position);
  if (position_it == attribute_locations.end()
   || vertex_buffer.address == 0u
   || index_buffer.address  == 0u
   || mesh.topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
   || mesh.submeshes.empty()) {
    return false;
  }

  std::vector<VkAccelerationStructureGeometryKHR> geometries{};
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges{};
  std::vector<uint32_t> primitive_counts{};
  if (!MakeGeometries(
        asset, mesh, position_it->second,
        vertex_buffer.address, index_buffer.address, {},
        geometries, ranges, primitive_counts)) {
    return false;
  }

  // (addresses do not matter for the size query)
  VkAccelerationStructureBuildGeometryInfoKHR const build_info{
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    .flags = kDynamicBuildFlags,
    .mode  = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    .geometryCount = static_cast<uint32_t>(geometries.size()),
    .pGeometries   = geometries.data(),
  };
  VkAccelerationStructureBuildSizesInfoKHR sizes{
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
  };
  vkGetAccelerationStructureBuildSizesKHR(
    context.device(),
    VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
    &build_info,
    primitive_counts.data(),
    &sizes
  );

  out.build_sizes_info = sizes;
  out.buffer = context.createBuffer(
    "DynamicBLAS",
    sizes.accelerationStructureSize,
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
    | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
  );
  if (!out.buffer.valid()) {
    LOGE("accel::CreateDynamicBLAS: failed to allocate a BLAS buffer.");
    out = {};
    return false;
  }

  VkAccelerationStructureCreateInfoKHR const create_info{
    .sType  = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
    .buffer = out.buffer.buffer,
    .size   = sizes.accelerationStructureSize,
    .type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
  };
  CHECK_VK(vkCreateAccelerationStructureKHR(
    context.device(), &create_info, nullptr, &out.handle
  ));

  VkAccelerationStructureDeviceAddressInfoKHR const address_info{
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
    .accelerationStructure = out.handle,
  };
  out.address = vkGetAccelerationStructureDeviceAddressKHR(context.device(), &address_info);
  out.scratch_size = std::max(sizes.buildScratchSize, sizes.updateScratchSize);
  return true;
}

// ----------------------------------------------------------------------------

void DestroyBLAS(Context const& context, DeviceAsset& asset) {
  for (auto& mesh : asset.meshes) {
    Destroy(context, mesh.blas);
  }
}

// ----------------------------------------------------------------------------

void Destroy(Context const& context, backend::AccelerationStructure& as) {
  if (as.handle != VK_NULL_HANDLE) {
    vkDestroyAccelerationStructureKHR(context.device(), as.handle, nullptr);
  }
  if (as.buffer.valid()) {
    context.destroyBuffer(as.buffer);
  }
  as = {};
}

// ----------------------------------------------------------------------------

bool BuildDynamicBLAS(
  VkCommandBuffer cmd,
  DeviceAsset const& asset,
  DeviceMesh const& mesh,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer,
  Geometry::AttributeLocationMap const& attribute_locations,
  std::span<VkDeviceAddress const> vertex_addresses,
  VkDeviceAddress scratch_address,
  backend::AccelerationStructure const& blas,
  bool update
) {
  auto const position_it = attribute_locations.find(Geometry::AttributeType::Position);
  if (!blas.valid() || position_it == attribute_locations.end() || scratch_address == 0u) {
    return false;
  }

  std::vector<VkAccelerationStructureGeometryKHR> geometries{};
  std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges{};
  std::vector<uint32_t> primitive_counts{};
  if (!MakeGeometries(
        asset, mesh, position_it->second,
        vertex_buffer.address, index_buffer.address, vertex_addresses,
        geometries, ranges, primitive_counts)) {
    return false;
  }

  VkAccelerationStructureBuildGeometryInfoKHR const build_info{
    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
    .type  = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
    .flags = kDynamicBuildFlags,
    .mode  = update ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                    : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
    .srcAccelerationStructure = update ? blas.handle : VK_NULL_HANDLE,
    .dstAccelerationStructure = blas.handle,
    .geometryCount = static_cast<uint32_t>(geometries.size()),
    .pGeometries   = geometries.data(),
    .scratchData   = { .deviceAddress = scratch_address },
  };
  auto const* range_ptr = ranges.data();
  vkCmdBuildAccelerationStructuresKHR(cmd, 1u, &build_info, &range_ptr);

  // Scratch reuse between builds + BLAS visible to the TLAS build.
  VkMemoryBarrier2 const barrier{
    .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
    .srcStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
    .dstStageMask  = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
    .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
                   | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
  };
  VkDependencyInfo const dependency{
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .memoryBarrierCount = 1u,
    .pMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cmd, &dependency);
  return true;
}

} // namespace accel