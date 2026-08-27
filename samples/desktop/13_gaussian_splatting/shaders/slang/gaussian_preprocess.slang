// ----------------------------------------------------------------------------

#include "../interop.h"

// ----------------------------------------------------------------------------

[[vk::push_constant]]
PushConstant pushConstant;

// ----------------------------------------------------------------------------

float3x3 quaternionToMatrix(
  in float4 qrot
) {
  let x = qrot.x;
  let y = qrot.y;
  let z = qrot.z;
  let r = qrot.w; //

  // Column major layout
  return float3x3(
    1.0 - 2.0 * (y*y + z*z),       2.0 * (x*y - r*z),       2.0 * (x*z + r*y),
          2.0 * (x*y + r*z), 1.0 - 2.0 * (x*x + z*z),       2.0 * (y*z - r*x),
          2.0 * (x*z - r*y),       2.0 * (y*z + r*x), 1.0 - 2.0 * (x*x + y*y)
  );
}

// ----------------

float3x3 computeCovariance3D(
  in float3 scale,
  in float4 qrot
) {
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

// ----------------

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

[shader("compute")]
[numthreads(kCompute_Preprocess_kernelSize_x, 1, 1)]
void main(
  uint gid : SV_DispatchThreadID
)
{
  if (gid >= pushConstant.numElems) {
    return;
  }

  const float kNearZCull        = -0.2f;
  const float kNDCMargin        = 0.3f;
  const float kCovariance2DBlur = 0.3f;
  const float kMaxSplatRadius   = 1024.0f;
  const float kTileSize         = 16.0f;

  // Inputs
  let uData    = *((UniformBufferData*)pushConstant.uniform_addr);
  let gaussian = ((GaussianData*)pushConstant.gaussian_addr)[gid];

  let viewMatrix: float4x4 = uData.viewMatrix;
  let projMatrix: float4x4 = uData.projectionMatrix;
  let resolution: float2   = uData.resolution;

  // Output
  SplatOutput *output     = &(((SplatOutput*)pushConstant.splat_addr)[gid]);
  SplatTileInfo *tileInfo = &(((SplatTileInfo*)pushConstant.tile_addr)[gid]);

  // Reset tile overlapped count.
  tileInfo.count = 0;

  // 1. Culling.

  // Transform to view space.
  let pos_view = mul(viewMatrix, float4(gaussian.position.xyz, 1.0));

  // Near cull (-Z forward).
  if (pos_view.z > kNearZCull) {
    return;
  }

  // Transform to NDC space.
  let pos_clip = mul(projMatrix, pos_view);
  let pos_ndc = pos_clip.xyz / pos_clip.w;

  // Cull outside the NDC.
  if (any(abs(pos_ndc.xy) > float2(1.0f + kNDCMargin))) {
    return;
  }

  // ----

  // 2. Calculate Covariances.

  // (eq. 6) Ellipsoid covariance matrix of a 3D Gaussian.
  let cov3D = computeCovariance3D(gaussian.scale.xyz, gaussian.rotation);

  // Extract upper 3x3 rotation from column-major ViewMatrix.
  let W = float3x3(
    viewMatrix[0].x, viewMatrix[1].x, viewMatrix[2].x,
    viewMatrix[0].y, viewMatrix[1].y, viewMatrix[2].y,
    viewMatrix[0].z, viewMatrix[1].z, viewMatrix[2].z
  );

  // (eq. 5) Project the 3D Gaussian to 2D.
  let J = computeJacobian(pos_view.xyz, uData.tanFov, uData.focal);
  let T = mul(J, W);
  let cov2D = mul(T, mul(cov3D, transpose(T)));

  // ----

  // 3. Calculate the conic parameters.

  // Low-pass filter (blur variance in screen space XY to avoid aliasing).
  let cov_a = cov2D[0][0] + kCovariance2DBlur;
  let cov_b = cov2D[0][1];
  let cov_c = cov2D[1][1] + kCovariance2DBlur;

  // Invert the 2D covariant matrix to obtain the conic parameters.
  let det = cov_a * cov_c - cov_b * cov_b;
  if (det == 0.0f) {
    return;
  }
  let conic = (1.0f / det) * float3(cov_c, -cov_b, cov_a);

  // ----

  // 4. Screen radius via eigenvalues.

  let mid     = 0.5f * (cov_a + cov_c);
  let term    = sqrt(max(0.1f, mid * mid - det));
  let lambda1 = mid + term;
  let lambda2 = mid - term;
  let radius  = ceil(3.0f * sqrt(max(lambda1, lambda2)));

  if (radius <= 0.0f || radius > kMaxSplatRadius) {
    return;
  }

  // ----

  // 5. Determine tiles coverage.

  let pos_screen = (pos_ndc.xy * 0.5f + 0.5f) * resolution;
  let minPixel = pos_screen - radius;
  let maxPixel = pos_screen + radius;

  // Find tiles bounds [minTile, maxTile)
  let tile_grid_size = ceil(resolution / kTileSize);
  let minTile = uint2(clamp(floor(minPixel / kTileSize), float2(0.0f), tile_grid_size - 1.0f));
  let maxTile = uint2(clamp(ceil(maxPixel / kTileSize), float2(0.0f), tile_grid_size));

  // Cull splats outside the grid.
  if (any(minTile >= maxTile)) {
    return;
  }

  tileInfo->count  = (maxTile.x - minTile.x) * (maxTile.y - minTile.y);
  tileInfo->offset = 0; //

  // ----

  // TODO : compute color from Spherical Harmonics.

  output->color       = gaussian.color; // (redundant)
  output->conic       = conic;
  output->depth       = -pos_view.z;
  output->screen_pos  = pos_screen;
}

// ----------------------------------------------------------------------------
