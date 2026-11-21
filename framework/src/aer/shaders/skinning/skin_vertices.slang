#include <skinning/interop.h>
#include <material/interop.h> // for Vertex

/* -------------------------------------------------------------------------- */

[[vk::push_constant]]
SkinningPushConstant pc;

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
  o.normal   = normalize(mul(m, float4(v.normal, 0.0f)).xyz);
  o.tangent  = float4(normalize(mul(m, float4(v.tangent.xyz, 0.0f)).xyz), v.tangent.w);

  dst[i] = o;
}

/* -------------------------------------------------------------------------- */
