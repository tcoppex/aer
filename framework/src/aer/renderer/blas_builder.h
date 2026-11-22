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

/* Destroy an acceleration structure and its buffer. Safe on an empty one. */
void Destroy(Context const& context, backend::AccelerationStructure& as);

} // namespace accel

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_BLAS_BUILDER_H_