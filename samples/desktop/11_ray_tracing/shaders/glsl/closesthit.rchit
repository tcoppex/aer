#version 460

// -----------------------------------------------------------------------------

#include "../interop.h"

#include <shared/maths.glsl>
#include <shared/random.glsl>

// -----------------------------------------------------------------------------
// Topology

layout(buffer_reference, scalar)
buffer Vertices {
  Vertex v[];
};

layout(buffer_reference, scalar)
buffer Indices {
  uint u32[];
};

layout(buffer_reference, scalar)
buffer InstanceDataBufferRef {
  RTInstanceData instances[];
};

#include <shared/rt_unpack_geometry.glsl>

// -----------------------------------------------------------------------------

hitAttributeEXT vec2 hitAttribs;

layout(location = 0) rayPayloadInEXT HitPayload_t payload;

layout(scalar, set = kDescriptorSet_Internal, binding = kDescriptorSetBinding_RayTracing_MaterialSBO)
buffer RayTracingMaterialSBO_ {
  RayTracingMaterial materials[];
};

layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_Textures)
uniform sampler2D uTextureChannels[];

layout(push_constant, scalar)
uniform PushConstant_ {
  PushConstant pushConstant;
};

// -----------------------------------------------------------------------------

void main() {
  // ----------------------------
  // TOPOLOGY.

  const uint primitive_id = gl_PrimitiveID;
  const uint material_id  = gl_InstanceCustomIndexEXT;

  RTInstanceData instance = GetRTInstanceData();
  Triangle_t tri = unpack_triangle(instance.vertexAddr, instance.indexAddr, primitive_id);
  Vertex v = calculate_vertex(tri, hitAttribs);

  // ----------------------------
  // MATERIAL.

  const uint kInvalidIndexU24 = 0x00FFFFFF;
  uint material_type = kRayTracingMaterialType_Diffuse;

  vec3 emissive = vec3(0.0f);
  vec4 color = vec4(1.0f);

  if (material_id != kInvalidIndexU24)
  {
    RayTracingMaterial mat = materials[nonuniformEXT(material_id)];

    vec4 emissive_base = texture(GetTexture(mat.emissive_texture_id), v.texcoord);
    emissive = mix(vec3(1.0f), emissive_base.rgb, emissive_base.a)
             * mat.emissive_factor
             ;

    color = texture(GetTexture(mat.diffuse_texture_id), v.texcoord)
          * mat.diffuse_factor
          ;

    const vec4 orm = texture(GetTexture(mat.orm_texture_id), v.texcoord);
    const float roughness = mat.roughness_factor * mix(1.0, max(orm.y, 1e-3f), orm.w);
    const float metallic = mat.metallic_factor * mix(1.0, orm.z, orm.w);

    if (any(greaterThan(emissive, vec3(1.e-2f))))
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

  if (material_type == kRayTracingMaterialType_Diffuse) {
    // Cosine-weighted hemisphere sampling
    vec3 tangent, bitangent;
    build_tangent_basis(v.normal, tangent, bitangent);

    vec3 localDir = sample_hemisphere_cosine(rand2f(payload.rngState));
    vec3 newDir = normalize(localDir.x * tangent +
                            localDir.y * bitangent +
                            localDir.z * v.normal);

    payload.origin = v.position + v.normal * 1e-3;
    payload.direction = newDir;
    payload.throughput *= color.rgb;
  }

  if (material_type == kRayTracingMaterialType_Mirror)
  {
    vec3 reflDir = reflect(normalize(gl_WorldRayDirectionEXT), v.normal);
    payload.origin = v.position + v.normal * 1e-3;
    payload.direction = reflDir;
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

  payload.depth += 1;
}

// -----------------------------------------------------------------------------
