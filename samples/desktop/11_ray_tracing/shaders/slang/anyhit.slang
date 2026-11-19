// ----------------------------------------------------------------------------

#include "../interop.h"

import shared.rt_geometry;

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSet_Scene_Textures, kDescriptorSet_Scene)]]
Sampler2D uTextureChannels[];

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("anyhit")]
void main(
  inout HitPayload_t payload,
  BuiltInTriangleIntersectionAttributes attr
) {
  let geo = GetRTGeometry();
  let tri = rt_geometry::unpack_triangle(geo, PrimitiveIndex());

  let uv = rt_geometry::calculate_texcoord(
    tri, rt_geometry::barycenter_from_hit(attr.barycentrics)
  );

  var opacity = 1.0f;

  if (geo.material_address != 0) {
    let mat = GetRayTracingMaterial(geo);

    let albedo = GetTexture(mat.diffuse_texture_id).SampleLevel(uv, 0.0f);
    let color = mat.diffuse_factor * albedo;

    opacity = (color.a > mat.alpha_cutoff) ? 1.0f : 0.0f;
  }

  if (opacity < 0.99f) {
    IgnoreHit();
  }
}

// ----------------------------------------------------------------------------
