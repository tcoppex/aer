#ifndef AER_RENDERER_BLAS_BUILDER_H_
#define AER_RENDERER_BLAS_BUILDER_H_

#include <span>

#include "aer/core/common.h"
#include "aer/platform/vulkan/context.h"
#include "aer/platform/vulkan/types.h"
#include "aer/renderer/device_asset.h"
#include "aer/scene/geometry.h"

/* -------------------------------------------------------------------------- */

// [move to GpuPool?]

namespace accel {

/**
 * Build one BLAS per DeviceMesh of an asset.
 * Reads device data only (pool buffers + DeviceAsset records).
 *
 * Returns true if every mesh was built.
 */
bool BuildBLAS(
  Context const& context,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer,
  Geometry::AttributeLocationMap const& attribute_locations,
  DeviceAsset& asset
);

/* Destroy every BLAS of an asset. */
void DestroyBLAS(Context const& context, DeviceAsset& asset);

/* Create the storage of a dynamic BLAS compatible with `mesh`. */
bool CreateDynamicBLAS(
  Context const& context,
  DeviceAsset const& asset,
  DeviceMesh const& mesh,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer,
  Geometry::AttributeLocationMap const& attribute_locations,
  backend::AccelerationStructure &dynamic_blas
);

/* Destroy an acceleration structure and its buffer. Safe on an empty one. */
void Destroy(Context const& context, backend::AccelerationStructure& as);

/* Record a full build of a dynamic BLAS (BUILD, no refit). */
bool BuildDynamicBLAS(
  VkCommandBuffer cmd,
  DeviceAsset const& asset,
  DeviceMesh const& mesh,
  backend::Buffer const& vertex_buffer,
  backend::Buffer const& index_buffer,
  Geometry::AttributeLocationMap const& attribute_locations,
  std::span<VkDeviceAddress const> vertex_addresses,
  VkDeviceAddress scratch_address,
  backend::AccelerationStructure const& dynamic_blas
);

} // namespace accel

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_BLAS_BUILDER_H_