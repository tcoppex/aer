// ----------------------------------------------------------------------------

#include "../interop.h"

#include <shared/rt_unpack_geometry.glsl> //

import shared.random;
import shared.constants;

// ----------------------------------------------------------------------------

[[vk::binding(kDescriptorSet_Scene_Textures, kDescriptorSet_Scene)]]
Sampler2D uTextureChannels[];

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

void build_tangent_basis(in float3 N, out float3 T, out float3 B) {
  if (abs(N.z) < 0.999) {
    T = normalize(cross(N, float3(0,0,1)));
  } else {
    T = normalize(cross(N, float3(0,1,0)));
  }
  B = cross(N, T);
}

float3 sample_hemisphere_cosine(float2 u) {
  let r = sqrt(u.x);
  let theta = constants.TwoPi * u.y;
  return float3(r * cos(theta), r * sin(theta), sqrt(max(0.0, 1.0 - u.x)));
}

// ----------------------------------------------------------------------------

[shader("closesthit")]
void main(
  inout HitPayload_t payload,
  BuiltInTriangleIntersectionAttributes attr
) {
  // ----------------------------
  // TOPOLOGY.

  let kInvalidIndexU24 = 0x00FFFFFF;
  let materialID  = InstanceIndex();
  let primitiveID = PrimitiveIndex();

  let instance = GetRTInstanceData();

  let tri = unpack_triangle(
    instance.vertexAddr, instance.indexAddr, primitiveID
  );

  let v = calculate_vertex(
    tri,
    attr.barycentrics,
    ObjectToWorld(),
    WorldToObject()
  );

  // ----------------------------
  // MATERIAL.

  var material_type = kRayTracingMaterialType_Diffuse;

  var emissive = float3(0.0f);
  var color = float4(1.0f);

  if (materialID != kInvalidIndexU24)
  {
    let mat = GetRayTracingMaterial(materialID);

    let emissive_base = GetTexture(mat.emissive_texture_id).SampleLevel(v.texcoord, 0.0f);

    emissive = lerp(float3(1.0f), emissive_base.rgb, emissive_base.a)
             * mat.emissive_factor
             ;

    color = GetTexture(mat.diffuse_texture_id).SampleLevel(v.texcoord, 0.0f)
          * mat.diffuse_factor
          ;

    let orm = GetTexture(mat.orm_texture_id).SampleLevel(v.texcoord, 0.0f);
    let roughness = mat.roughness_factor * lerp(1.0, max(orm.y, 1e-3f), orm.w);
    let metallic = mat.metallic_factor * lerp(1.0, orm.z, orm.w);

    if (any(emissive > float3(1.e-2f)))
    {
      material_type = kRayTracingMaterialType_Emissive;
    }
    else if ((metallic > 0.01f) && (roughness < 0.05))
    {
      material_type = kRayTracingMaterialType_Mirror;
    }
    else
    {
      material_type = kRayTracingMaterialType_Diffuse;
    }
  }

  // ----------------------------
  // SHADING.

  float3 N = normalize(v.normal);
  if (dot(N, WorldRayDirection()) > 0.0f) {
    N = -N;
  }

  if (material_type == kRayTracingMaterialType_Emissive)
  {
    payload.radiance = payload.throughput
                     * emissive
                     * pushConstant.light_intensity
                     ;
    payload.done = 1;
    return;
  }
  else
  if (material_type == kRayTracingMaterialType_Mirror)
  {
    let reflDir = reflect(WorldRayDirection(), N);
    payload.origin = v.position + N * 1e-3;
    payload.direction = reflDir;
  }
  else // default
  if (material_type == kRayTracingMaterialType_Diffuse)
  {
    // Cosine-weighted hemisphere sampling
    float3 tangent, bitangent;
    build_tangent_basis(N, tangent, bitangent);

    let localDir = sample_hemisphere_cosine(random.rand2f(payload.rngState));
    let newDir = normalize(localDir.x * tangent +
                           localDir.y * bitangent +
                           localDir.z * N);

    payload.origin = v.position + N * 1e-3;
    payload.direction = newDir;
    payload.throughput *= color.rgb;
  }

  payload.depth += 1;
}

// ----------------------------------------------------------------------------

