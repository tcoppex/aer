// ----------------------------------------------------------------------------

#include "../interop.h"
#include <shared/rt_unpack_geometry.glsl>

import shared.random;

// ----------------------------------------------------------------------------

RWTexture2D<float4> uAccumImage;

[[vk::binding(kDescriptorSet_Scene_Textures, kDescriptorSet_Scene)]]
Sampler2D uTextureChannels[];

RaytracingAccelerationStructure topLevelAS;

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

[shader("raygeneration")]
void main() {
  let raysIndex: uint3      = DispatchRaysIndex();
  let raysDimension: uint3  = DispatchRaysDimensions();

  let frameData             = GetFrameData();
  let camera                = GetCameraData(frameData, 0);
  let origin                = mul(camera.invViewMatrix, float4(0, 0, 0, 1));

  let pixel                 = int2(raysIndex.xy);
  let pixelCenter           = float2(pixel) + float2(0.5);
  let sampleSize            = 1.0f / float2(raysDimension.xy);
  let uv                    = pixelCenter * sampleSize;

  let numSamples: int       = pushConstant.num_samples;
  let maxDepth: int         = 16;

  // ----------

  HitPayload_t payload;

  // Initialize RNG for the pixel.
  var seed_params = uint3(raysIndex.xy, pushConstant.accumulation_frame_count);
  payload.rngState = random.xxhash32(seed_params);

  // Normalized device coordinates in [-1, 1]
  var ndc: float2 = uv * 2.0 - 1.0;
  // (flip Y to be consistent with the rasterizer viewport flip..)
  ndc.y = -ndc.y;

  var color = float3(0.0);

  for (int s = 0; s < numSamples; s++)
  {
    let jitter: float2 = pushConstant.jitter_factor
                       * (random.rand2f(payload.rngState) - 0.5)
                       * sampleSize
                       ;

    let target: float4 = mul(
      camera.invProjectionMatrix,
      float4(ndc + jitter, 0, 1)
    );

    let direction: float4 = mul(
      camera.invViewMatrix,
      float4(normalize(target.xyz), 0)
    );

    let tmin = 1e-3;
    let tmax = 1e32;

    payload.origin = origin.xyz;
    payload.direction = direction.xyz;
    payload.radiance = float3(0.0);
    payload.throughput = float3(1.0);
    payload.depth = 0;
    payload.done = 0;

    RayDesc ray;
    for (int depth = 0; depth < maxDepth; ++depth) {
      ray.Origin = payload.origin;
      ray.Direction = payload.direction;
      ray.TMin = tmin;
      ray.TMax = tmax;

      TraceRay(
        topLevelAS,
        RAY_FLAG_NONE,
        0xff,
        0, // sbt record offset
        0, // sbt record stride
        0, // miss shader index
        ray,
        payload
      );
      russianRoulette(payload);

      if ((payload.done == 1)
       || (dot(payload.throughput, payload.throughput) < 0.001)
      ) {
        break;
      }
    }
    color += payload.radiance;
  }
  color /= numSamples;

  let prevSample: float3 = max(float3(0.0f), uAccumImage[pixel].rgb);
  let newSample: float3  = clamp(color, 0.0f, 100.0f);
  let frameCount = pushConstant.accumulation_frame_count + 1;

  color = lerp(prevSample, newSample, 1.0 / frameCount);

  // ------

  uAccumImage[pixel] = float4(color, 1.0);
}

void russianRoulette(inout HitPayload_t payload) {
  if (payload.depth > 4) {
    float p = max(payload.throughput.r,
                  max(payload.throughput.g, payload.throughput.b));
    p = clamp(p, 0.05, 0.95);

    if (random.randf(payload.rngState) > p) {
      payload.done = 1;
      return;
    } else {
      payload.throughput /= p;
    }
  }
}

// ----------------------------------------------------------------------------

[shader("anyhit")]
void anyhitMain(
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
    let mat = *(RayTracingMaterial*)(pushConstant.material_buffer_address + materialID);

    let color = GetTexture(mat.diffuse_texture_id).Sample(uv)
              * mat.diffuse_factor
              ;
    opacity = (color.a > mat.alpha_cutoff) ? 1.0f : 0.0;
  }

  if (opacity < 0.99f) {
    IgnoreHit();
  }
}


// ----------------------------------------------------------------------------

[shader("closesthit")]
void closesthitMain(
  inout HitPayload_t payload,
  BuiltInTriangleIntersectionAttributes attr
) {
  // TODO
}

// ----------------------------------------------------------------------------

[shader("miss")]
void missMain(
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

