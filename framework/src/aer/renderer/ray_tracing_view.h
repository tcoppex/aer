#ifndef AER_RENDERER_RAY_TRACING_VIEW_H_
#define AER_RENDERER_RAY_TRACING_VIEW_H_

#include "aer/platform/vulkan/types.h"

/* -------------------------------------------------------------------------- */

/**
 * What a ray tracing fx needs to trace the scene for the current frame.
 * Valid for this frame only (point to per-frame ring resources).
 */
struct RayTracingView {
  /* Top level acceleration structure of the frame. */
  VkAccelerationStructureKHR tlas{VK_NULL_HANDLE};

  /* material_shader_interop::RTGeometryData[] */
  VkDeviceAddress geometry_table_address{};

  /* material_shader_interop::FrameData of the frame (cameras, time...). */
  VkDeviceAddress frame_data_address{};

  /* Changes whenever the traced content changes. */
  uint64_t generation{};

  [[nodiscard]]
  bool valid() const noexcept { return tlas != VK_NULL_HANDLE; }
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_RAY_TRACING_VIEW_H_