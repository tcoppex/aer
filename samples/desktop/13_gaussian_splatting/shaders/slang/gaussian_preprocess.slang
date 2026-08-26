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
  let tz = abs(position.z);

  // Limit position to avoid distortion artifacts on screen edges
  let limx = 1.3f * tanFov.x;
  let limy = 1.3f * tanFov.y;
  let txtz = clamp(position.x / tz, -limx, limx);
  let tytz = clamp(position.y / tz, -limy, limy);

  // Derivative matrix to approximate a 2D projection
  let J = float3x3(
    focal.x / tz,   0.0f,           -(focal.x * txtz) / tz,
    0.0f,           focal.y / tz,   -(focal.y * tytz) / tz,
    0.0f,           0.0f,           0.0f
  );

  return J;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kCompute_Preprocess_kernelSize_x, 1, 1)]
void main( uint gid : SV_DispatchThreadID )
{
  if (gid >= pushConstant.numElems) {
    return;
  }

  const float kNearZCull        = -0.2f;
  const float kNDCMargin        = 0.3f;
  const float kCovariance2DBlur = 0.3f;
  const float kMaxSplatRadius   = 1024.0f;

  // Retrieve the thread's Gaussian.
  let gaussian = ((GaussianData*)pushConstant.gaussian_addr_)[gid];

  // Gaussian mean (/ world position).
  let g_mean: float3 = gaussian.position.xyz;

  // Camera's data.
  let viewMatrix: float4x4 = pushConstant.viewMatrix;
  let projMatrix: float4x4 = pushConstant.projectionMatrix;

  // Transform to view space.
  let pos_view = mul(viewMatrix, float4(g_mean, 1.0));

  // ----

  // 1. Culling.

  // Near cull (left-handed system)
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

  // 2. Calculate Covariances

  // (eq. 6) Ellipsoid covariance matrix of a 3D Gaussian.
  let cov3D = computeCovariance3D(gaussian.scale.xyz, gaussian.rotation);

  // (eq. 5) Project the 3D Gaussian to 2D (EWA splatting).
  let J = computeJacobian(pos_view.xyz, pushConstant.tanFov, pushConstant.focal);
  let W = float3x3(
    viewMatrix[0].xyz,  // right
    viewMatrix[1].xyz,  // up
    viewMatrix[2].xyz   // forward (-z)
  );
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
  let inv_det = 1.0f / det;
  let conic = inv_det * float3(cov_c, -cov_b, cov_a);

  // ----

  // 4. Screen radius via eigenvalues

  // half the matrix trace, representing the center point of the eigenvalues.
  let mid = 0.5f * (cov_a + cov_c);
  // square root discriminant, filtered to prevent FP rounding errors.
  let term = sqrt(max(0.1f, mid * mid - det));
  // magnitude of maximimum variance along the Gaussian's primary axis
  let lambda1 = mid + term;

  // 3-sigma radius (capture >99% of the gaussian's total probability energy)
  let radius = ceil(3.0f * sqrt(lambda1));

  // Guard against extrem splat sizes.
  if (radius <= 0.0f || radius > kMaxSplatRadius) {
    return;
  }

  // TO CONTINUE

  // Outputs : position2D, depth (/ sortkey), covariance2D, RGBA color+opacity
  // binned ?
}

// ----------------------------------------------------------------------------