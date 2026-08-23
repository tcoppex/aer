// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("miss")]
void main(
  inout HitPayload_t payload
) {
  let rayDirection = WorldRayDirection();

  let dawn = float3(1.0, 0.45, 0.05);
  let sky  = float3(0.2, 0.40, 0.80);
  let t    = pow(0.5 * (rayDirection.y + 1.0), 0.7);

  let skyColor = lerp(dawn, sky, t)
               * pushConstant.sky_intensity
               ;

  payload.radiance += skyColor * payload.throughput;
  payload.done = 1;
}

// ----------------------------------------------------------------------------

