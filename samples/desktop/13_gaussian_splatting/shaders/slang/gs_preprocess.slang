// ----------------------------------------------------------------------------
//
//  Preprocess stage of the Gaussian Splatting algorithm.
//
//   * Camera are expected to be right-handed, -Z forward, column-major layout.
//   * Slangc must use -matrix-layout-column-major
//
// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

float3x3 quaternionToMatrix(in float4 qrot) {
  let x = qrot.x;
  let y = qrot.y;
  let z = qrot.z;
  let r = qrot.w;

  return float3x3(
    1.0 - 2.0 * (y*y + z*z),       2.0 * (x*y - r*z),       2.0 * (x*z + r*y),
          2.0 * (x*y + r*z), 1.0 - 2.0 * (x*x + z*z),       2.0 * (y*z - r*x),
          2.0 * (x*z - r*y),       2.0 * (y*z + r*x), 1.0 - 2.0 * (x*x + y*y)
  );
}

float3x3 computeCovariance3D(in float3 scale, in float4 qrot) {
  let s = exp(scale);
  let S = float3x3(
    s.x, 0.0, 0.0,
    0.0, s.y, 0.0,
    0.0, 0.0, s.z
  );
  let R = quaternionToMatrix(normalize(qrot));
  let M = mul(R, S);
  return mul(M, transpose(M));
}

float3x3 computeJacobian(
  in float3 position,
  in float2 tanFov,
  in float2 focal
) {
  // force tz positive to represent distance along forward
  let tz = -position.z;

  // Limit position to avoid distortion artifacts on screen edges
  let limx = 1.3f * tanFov.x;
  let limy = 1.3f * tanFov.y;
  let txtz = clamp(position.x / tz, -limx, limx);
  let tytz = clamp(position.y / tz, -limy, limy);

  // Partial derivative matrix to approximate a 2D projection
  return float3x3(
    focal.x / tz,   0.0f,           - (focal.x * txtz) / tz,
    0.0f,           focal.y / tz,   - (focal.y * tytz) / tz,
    0.0f,           0.0f,           0.0f
  );
}

// ----------------------------------------------------------------------------

static const float SH_C0 = 0.28209479177387814f;

static const float SH_C1 = 0.4886025119029199f;

static const float SH_C2[5] = {
  1.0925484305920792f,
 -1.0925484305920792f,
  0.31539156525252005f,
 -1.0925484305920792f,
  0.5462742152960396f
};

static const float SH_C3[7] = {
 -0.5900435899266435f,
  2.890611442640554f,
 -0.4570457994644658f,
  0.3731763325901154f,
 -0.4570457994644658f,
  1.445305721320277f,
 -0.5900435899266435f
};

float3 computeColorFromSH(in float3 dir, in float shc[48]) {
  let x = dir.x;
  let y = dir.y;
  let z = dir.z;
  let xx = x * x;
  let yy = y * y;
  let zz = z * z;
  let xy = x * y;
  let yz = y * z;
  let xz = x * z;

  float3 result = float3(0);

  result += SH_C0 * float3(shc[0],shc[1],shc[2]);

  result +=
    - SH_C1 * float3(shc[3],  shc[4],  shc[5]) * x
    + SH_C1 * float3(shc[6],  shc[7],  shc[8]) * y
    - SH_C1 * float3(shc[9], shc[10], shc[11]) * z
  ;

  result +=
      SH_C2[0] * float3(shc[12], shc[13], shc[14]) * xy
    + SH_C2[1] * float3(shc[15], shc[16], shc[17]) * yz
    + SH_C2[2] * float3(shc[18], shc[19], shc[20]) * (2.0f * zz - xx - yy)
    + SH_C2[3] * float3(shc[21], shc[22], shc[23]) * xz
    + SH_C2[4] * float3(shc[24], shc[25], shc[26]) * (xx - yy)
  ;

  result +=
      SH_C3[0] * float3(shc[27], shc[28], shc[29]) * y * (3.0f * xx - yy)
    + SH_C3[1] * float3(shc[30], shc[31], shc[32]) * xy * z
    + SH_C3[2] * float3(shc[33], shc[34], shc[35]) * y * (4.0f * zz - xx - yy)
    + SH_C3[3] * float3(shc[36], shc[37], shc[38]) * z * (2.0f * zz - 3.0f * xx - 3.0f * yy)
    + SH_C3[4] * float3(shc[39], shc[40], shc[41]) * x * (4.0f * zz - xx - yy)
    + SH_C3[5] * float3(shc[42], shc[43], shc[44]) * z * (xx - yy)
    + SH_C3[6] * float3(shc[45], shc[46], shc[47]) * x * (xx - 3.0f * yy)
  ;

  return max(float3(0.0f), result + 0.5f);
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_Preprocess_kernelSize_x, 1, 1)]
void main(
  uint gid : SV_DispatchThreadID
)
{
  if (gid >= pushConstant.numElems) {
    return;
  }

  const float kMaxSplatRadius   = 1024.0f; //

  const float kOpacityCullLogit = -5.545f; // logit(1/255)
  const float kNearZCull        = -0.1f;
  const float kNDCMargin        = 0.3f;
  const float kCovariance2DBlur = 0.3f;
  const float kEpsilon          = 0.001f;

  const float3 kFlipY = float3(1.0, -1.0, 1.0);

  // --------------------------------------------------

  // Inputs
  let gaussian = ((GaussianData*)pushConstant.gaussian_addr)[gid];
  let gMean: float3    = gaussian.position.xyz * kFlipY;
  let gScale: float3   = gaussian.scale_opacity.xyz;
  let gOpacity: float  = gaussian.scale_opacity.w;

  let uData = *((UniformBufferData*)pushConstant.uniform_addr);
  let viewMatrix: float4x4 = uData.viewMatrix;
  let projMatrix: float4x4 = uData.projectionMatrix;
  let resolution: float2   = uData.resolution;

  // --------------------------------------------------

  // Outputs
  let output    = &(((SplatOutput*)pushConstant.splat_addr)[gid]);
  let tileCount = &(((uint32_t*)pushConstant.scan_input_addr)[gid]);

  // Reset tile overlapped count.
  *tileCount = 0;

  // --------------------------------------------------
  // 1. Culling.

  // Cull near-invisible splats.
  if (gOpacity < kOpacityCullLogit) {
    return;
  }

  // Transform to view space.
  var pos_view = mul(viewMatrix, float4(gMean, 1.0));
  pos_view.xyz *= kFlipY;

  // Near cull (-Z forward).
  if (pos_view.z > kNearZCull) {
    return;
  }

  // Transform to NDC space.
  let pos_clip = mul(projMatrix, pos_view);
  let pos_ndc = (pos_clip.xyz / pos_clip.w);

  // Cull outside the NDC.
  if ((pos_ndc.z > 1.0f)
    || any(abs(pos_ndc.xy) > float2(1.0f + kNDCMargin))) {
    return;
  }

  // --------------------------------------------------
  // 2. Calculate Covariances.

  // (eq. 6) Ellipsoid covariance matrix of a 3D Gaussian.
  let cov3D = computeCovariance3D(gScale, gaussian.rotation);

  // Extract upper 3x3 rotation from column-major ViewMatrix.
  let W = (float3x3)viewMatrix;

  // (eq. 5) Project the 3D Gaussian to 2D.
  let J = computeJacobian(pos_view.xyz, uData.tanFov, uData.focal);
  let T = mul(J, W);
  let cov2D = mul(T, mul(cov3D, transpose(T)));

  // --------------------------------------------------
  // 3. Calculate the conic parameters.

  // Low-pass filter (blur variance in screen space XY to avoid aliasing).
  let cov_a = cov2D[0][0] + kCovariance2DBlur;
  let cov_b = cov2D[0][1];
  let cov_c = cov2D[1][1] + kCovariance2DBlur;

  // Invert the 2D covariant matrix to obtain the conic parameters.
  let det = cov_a * cov_c - cov_b * cov_b;
  if (!(abs(det) > kEpsilon)) {
    return;
  }
  let conic = (1.0f / det) * float3(cov_c, -cov_b, cov_a);

  // --------------------------------------------------
  // 4. Screen radius via eigenvalues.

  let mid     = 0.5f * (cov_a + cov_c);
  let term    = sqrt(max(0.1f, mid * mid - det));
  let lambda1 = mid + term;
  let lambda2 = mid - term;
  let radius  = ceil(3.0f * sqrt(max(lambda1, lambda2)));

  if (!(radius > kEpsilon && radius <= kMaxSplatRadius)) {
    return;
  }

  // --------------------------------------------------
  // 5. Determine tiles coverage.

  let pos_screen = (pos_ndc.xy * 0.5f + 0.5f) * resolution;
  let minPixel = pos_screen - radius;
  let maxPixel = pos_screen + radius;

  let invTileRes: float = 1.0 / float(kTileResolution);

  // Find tiles bounds [minTile, maxTile)
  let tile_grid_size = ceil(resolution * invTileRes);
  let minTile = uint2(clamp(floor(minPixel * invTileRes), float2(0.0f), tile_grid_size - 1.0f));
  let maxTile = uint2(clamp(ceil(maxPixel * invTileRes), float2(0.0f), tile_grid_size));

  // Cull splats outside the grid.
  if (any(minTile >= maxTile)) {
    return;
  }

  // --------------------------------------------------
  // 6. Outputs.

  uint32_t count = (maxTile.x - minTile.x) * (maxTile.y - minTile.y);

  // [Unused, only for bugtracking]
  // [hacky] Force per gaussian tile output budget.
  // if (pushConstant.maxKeyValueCapacity > 0) {
  //   count = uint32_t(min(count, pushConstant.maxKeyValueCapacity));
  // }

  *tileCount = count;

  let view_dir  = normalize(pos_view.xyz);
  let rgb       = computeColorFromSH(view_dir, gaussian.shc);

  output->color       = float4(rgb, gOpacity);
  output->conic       = conic;
  output->depth       = - pos_view.z;
  output->screen_pos  = pos_screen;
  output->minTile     = minTile;
  output->maxTile     = maxTile;
}

// ----------------------------------------------------------------------------
