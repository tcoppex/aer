// ----------------------------------------------------------------------------

#include "../interop.h"

#include <shared/rt_unpack_geometry.glsl> //

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
  let kInvalidIndexU24 = 0x00FFFFFF;
  let materialID  = InstanceIndex();
  let primitiveID = PrimitiveIndex();
  let instance = GetRTInstanceData();

  let tri = unpack_triangle(
    instance.vertexAddr, instance.indexAddr, primitiveID
  );

  let uv = calculate_texcoord(tri, barycenter_from_hit(attr.barycentrics));

  var opacity = 1.0f;

  if (materialID != kInvalidIndexU24) {
    let mat = GetRayTracingMaterial(materialID);

    let albedo = GetTexture(mat.diffuse_texture_id).SampleLevel(uv, 0.0f);
    let color = mat.diffuse_factor * albedo;

    opacity = (color.a > mat.alpha_cutoff) ? 1.0f : 0.0f;
  }

  if (opacity < 0.99f) {
    IgnoreHit();
  }
}

// ----------------------------------------------------------------------------
