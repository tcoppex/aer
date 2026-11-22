#include <skinning/interop.h>
#include <material/interop.h> // for Vertex

/* -------------------------------------------------------------------------- */

[[vk::push_constant]]
SkinningPushConstant pc;

// ----------------------------------------------------------------------------

float3 transformNormal(float3x3 a, float3 n) {
  let c0  = cross(a[1], a[2]);
  let c1  = cross(a[2], a[0]);
  let c2  = cross(a[0], a[1]);
  let det = dot(a[0], c0);

  // mirrored transform to keep the normal on the outer side
  var r = float3(dot(c0, n), dot(c1, n), dot(c2, n));
  if (det < 0.0f) {
    r = -r;
  }

  // keep the bind normal when matrix is degenerate
  let len2 = dot(r, r);
  return (len2 > 1e-12f) ? r * rsqrt(len2) : n;
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kSkinningGroupSize, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  let i = tid.x;
  if (i >= pc.vertex_count) {
    return;
  }

  let src     = (Vertex*)pc.src_vertices;
  var dst     = (Vertex*)pc.dst_vertices;
  let skin    = (SkinVertex*)pc.skin_vertices;
  let palette = (float4x4*)pc.palette;

  let sv   = skin[i];
  let last = max(pc.joint_count, 1u) - 1u;

  float4x4 m = float4x4(0.0f);
  m += sv.weights.x * palette[min(sv.joints.x, last)];
  m += sv.weights.y * palette[min(sv.joints.y, last)];
  m += sv.weights.z * palette[min(sv.joints.z, last)];
  m += sv.weights.w * palette[min(sv.joints.w, last)];

  // Vertices without influence keep their bind pose.
  if (dot(sv.weights, float4(1.0f)) < 1e-4f) {
    m = float4x4(
      1.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      0.0f, 0.0f, 0.0f, 1.0f
    );
  }

  Vertex v = src[i];
  Vertex o = v;

  o.position = mul(m, float4(v.position, 1.0f)).xyz;
  o.normal   = transformNormal((float3x3)m, v.normal);
  o.tangent  = float4(normalize(mul(m, float4(v.tangent.xyz, 0.0f)).xyz), v.tangent.w);

  dst[i] = o;
}

/* -------------------------------------------------------------------------- */
