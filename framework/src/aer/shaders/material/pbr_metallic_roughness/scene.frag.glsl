#version 460
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : require

// ----------------------------------------------------------------------------

#include <material/interop.h>
#include <material/pbr_metallic_roughness/interop.h>
#include <shared/maths.glsl>
#include <shared/lighting/pbr.glsl>

// ----------------------------------------------------------------------------

layout(constant_id = 0) const bool constant_kUseAlphaCutoff = false;

layout(scalar, set = kDescriptorSet_Internal, binding = kDescriptorSet_Internal_MaterialSBO)
buffer MaterialSSBO_ {
  Material materials[];
};

// -- Frame --

layout(scalar, set = kDescriptorSet_Frame, binding = kDescriptorSet_Frame_FrameUBO)
uniform FrameUBO_ {
  FrameData uFrame;
};

// -- Scene resource (Textures & Image Based Lighting) --

layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_Textures)
uniform sampler2D[] uTextureChannels;

layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_IBL_Prefiltered)
uniform samplerCube uEnvMapPrefilterd;

layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_IBL_Irradiance)
uniform samplerCube uEnvMapIrradiance;

layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_IBL_SpecularBRDF)
uniform sampler2D uSpecularBRDF;

// layout(set = kDescriptorSet_Scene, binding = kDescriptorSet_Scene_LightSSBO)
// buffer LightSSBO_ {
//   LightInfo_t lights[];
// };

// -- Instance PushConstant --

layout(push_constant, scalar) uniform PushConstant_ {
  PushConstant pushConstant;
};

// ----------------------------------------------------------------------------

layout(location = 0) in vec3 vPositionWS;
layout(location = 1) in vec3 vNormalWS;
layout(location = 2) in vec4 vTangentWS;
layout(location = 3) in vec2 vTexcoord;

layout(location = 0) out vec4 fragColor;

// ----------------------------------------------------------------------------

#define TEXTURE_ATLAS(i)  uTextureChannels[nonuniformEXT(i)]

vec4 sample_DiffuseColor(in Material mat) {
  return texture(TEXTURE_ATLAS(mat.diffuse_texture_id), vTexcoord).rgba;
}

vec3 sample_NormalMap(in Material mat) {
  vec3 normal = texture(TEXTURE_ATLAS(mat.normal_texture_id), vTexcoord).rgb;
  return normalize(normal * 2.0 - 1.0);
}

// ----------------------------------------------------------------------------

vec3 calculate_world_space_normal(in vec3 normalWS, in vec4 tangentWS, in vec3 normalTS) {
#if 1
  vec3 normal = normalize(normalWS);

  // TBN basis to transform from tangent-space to world-space.
  // We do not have to normalize this interpolated vectors to get the TBN.
  vec3 tangent = normalize(tangentWS.xyz);
       tangent = clamp(tangent, vec3(-1), vec3(1)); //

  const vec3 bitangent = cross(normal, tangent) * tangentWS.w;
  const mat3 TBN = mat3(tangent, bitangent, normal);

  // World-space bump normal.
  return normalize(TBN * normalTS);
#else
  return vNormalWS;
#endif
}

FragInfo_t calculate_world_space_frag_info(
  in vec3 eyeWS,
  in vec3 posWS,
  in vec3 normalWS,
  in vec2 texCoord
) {
  FragInfo_t frag;

  frag.P        = posWS;
  frag.N        = normalWS;
  frag.V        = normalize(eyeWS - frag.P);
  frag.R        = reflect(-frag.V, frag.N);
  frag.uv       = texCoord;
  frag.n_dot_v  = dot(frag.N, frag.V);

  // Deal with double sided plane.
  // frag.N *= sign(frag.n_dot_v);

  frag.n_dot_v = saturate(frag.n_dot_v);

  return frag;
}

bool has_render_state(uint state_bits) {
  return (uFrame.renderer_states & state_bits) == state_bits;
}

PBRMetallicRoughness_Material_t calculate_pbr_material_data(
  in FragInfo_t frag,
  in Material mat,
  in vec3 mainColor
) {
  PBRMetallicRoughness_Material_t data;

  // Diffuse / Albedo.
  data.color = mainColor;

  // Emissive.
  vec4 emissive_base = texture(TEXTURE_ATLAS(mat.emissive_texture_id), frag.uv);
  data.emissive = mix(vec3(1.0f), emissive_base.xyz, emissive_base.a)
                * mat.emissive_factor
                ;
  // float flickering = pow(sin(1.4 * uFrame.cameraPos_Time.w), 2) + cos(0.7 * uFrame.cameraPos_Time.w);
  // data.emissive *= abs(flickering);

  // Roughness + Metallic.
  {
    const vec4 orm = texture(TEXTURE_ATLAS(mat.orm_texture_id), frag.uv);
    data.roughness = mat.roughness_factor * max(orm.y, 1e-3f);
    data.metallic = mat.metallic_factor * orm.z;
  }


  // Ambient Occlusion.
  const float ao = texture(TEXTURE_ATLAS(mat.occlusion_texture_id), frag.uv).x;
  data.ao = pow(ao, 1.5);

  // -- fragment derivative materials ---
  {
    // Irradiance.
    vec3 irradiance = vec3(1.0);
    if (has_render_state(kRendererState_IrradianceBit)) //
    {
      irradiance = texture(uEnvMapIrradiance, frag.N).rgb;
      irradiance *= 0.5; //
    }
    data.irradiance = irradiance;

    // Roughness based prefiltered specular.
    const float levels = log2(float(textureSize(uEnvMapPrefilterd, 0).x)) + 1.0;
    const float roughness_level = data.roughness * levels;
    data.prefiltered = textureLod( uEnvMapPrefilterd, frag.R, roughness_level).rgb;
    // data.prefiltered *= 0.5; //

    // -- might be incorrect --

    // Roughness based BRDF specular values.
    vec2 brdf_uv = vec2(frag.n_dot_v, data.roughness);
    data.BRDF = texture(uSpecularBRDF, brdf_uv).xy;
    data.BRDF = saturate(data.BRDF);
  }

  return data;
}

// ----------------------------------------------------------------------------

void main() {
  Material mat = materials[nonuniformEXT(pushConstant.material_index)];

  /* Diffuse. */
  const vec4 mainColor = sample_DiffuseColor(mat)
                       * mat.diffuse_factor
                       ;

  /* Early Alpha Test. */
  if (constant_kUseAlphaCutoff)
  {
    if (mainColor.a <= mat.alpha_cutoff) {
      discard;
    }
  }

  /* Detailled Normal. */
  const vec3 normalWS = calculate_world_space_normal(
    vNormalWS,
    vTangentWS,
    sample_NormalMap(mat)
  );

  /* Fragment infos.*/
  const FragInfo_t frag = calculate_world_space_frag_info(
    uFrame.cameraPos_Time.xyz,
    vPositionWS,
    normalWS,
    vTexcoord
  );

  PBRMetallicRoughness_Material_t pbr_data = calculate_pbr_material_data(
    frag,
    mat,
    mainColor.rgb
  );

  // -------------------------
  vec3 color = colorize_pbr(frag, pbr_data);

  if (gl_FragCoord.x > 0) {
    // color = vec3(pbr_data.BRDF, 0);
    // [TODO : explore mipmapping issue on specular !]
    // color = pbr_data.prefiltered;
  }

  float alpha = mainColor.a;
  // -------------------------

  fragColor = vec4(color, alpha);
}

// ----------------------------------------------------------------------------