// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk_binding(kDescriptorSetBinding_UniformBuffer, 0)]]
ConstantBuffer<UniformData> uData;

[[vk_binding(kDescriptorSetBinding_Sampler, 0)]]
Sampler2D uAtlas;

[[vk::push_constant]]
PushConstant pushConstant;

struct VSOutput {
  float4 position : SV_Position;
  float2 uv       : TEXCOORD0;
};

// ----------------------------------------------------------------------------

/**
 * Use one quad per glyph, procedurally generated (no vertex buffer).
 * Draw with TRIANGLE_STRIP, 4 vertices per glyph.
 **/
[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
  // Procedural triStrip quad [order: (0,0) (1,0) (0,1) (1,1)]
  let corner = float2(vertexID & 1u, vertexID >> 1u);

  float3 pos = float3(pushConstant.rect.xy + corner * pushConstant.rect.zw, 0.0f);
  if (pushConstant.animate) {
    pos.y += .04f * sin(pushConstant.instanceID + 3.8f * pushConstant.elapsedTime);
    pos.z += .13f * cos(pushConstant.instanceID + 2.1f * pushConstant.elapsedTime);
  }

  let uv = float2(
    lerp(pushConstant.uv.x, pushConstant.uv.y, corner.x),
    lerp(pushConstant.uv.w, pushConstant.uv.z, corner.y)
  );

  let viewProj = mul(uData.camera.projectionMatrix, uData.camera.viewMatrix);
  let MVP      = mul(viewProj, pushConstant.worldMatrix);

  VSOutput o;
  o.position = mul(MVP, float4(pos, 1.0));
  o.uv = uv;
  return o;
}

// ----------------------------------------------------------------------------

float median(float r, float g, float b) {
  return max(min(r, g), min(max(r, g), b));
}

float screenPxRange(float2 uv) {
  let unitRange = float2(uData.pxRange) / uData.atlasSize;
  let screenTexSize = float2(1.0) / fwidth(uv);
  return max(0.5 * dot(unitRange, screenTexSize), 1.0);
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target0 {
  const float4 mtsd = uAtlas.Sample(input.uv);
  const float spr = screenPxRange(input.uv);
  const float sd = median(mtsd.r, mtsd.g, mtsd.b);
  const float fillAlpha = clamp(spr * (sd - 0.5) + 0.5, 0.0, 1.0);

  float3 rgb = uData.fillColor.rgb;
  float alpha = fillAlpha;

  // Outline from the true SDF (alpha): smooth, correct offset on corners.
  if (uData.outlineWidth > 0.0) {
    const float outerAlpha = clamp(spr * (mtsd.a - (0.5 - uData.outlineWidth)) + 0.5, 0.0, 1.0);
    rgb = lerp(uData.outlineColor.rgb, uData.fillColor.rgb, fillAlpha);
    alpha = outerAlpha;
  }

  alpha *= uData.fillColor.a;
  if (alpha < 0.002) {
    discard;
  }
  return float4(rgb, alpha);
}

// ----------------------------------------------------------------------------
