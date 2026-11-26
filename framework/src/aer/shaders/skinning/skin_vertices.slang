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

float3 rotateByQuat(float4 q, float3 v) {
  return v + 2.0f * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

bool hasInfluence(SkinVertex sv) {
  return dot(sv.weights, float4(1.0f)) >= 1e-4f;
}

// Applies the weighted morph target deltas to a bind pose vertex.
void applyMorph(inout Vertex v, uint i) {
  let deltas  = (MorphDelta*)pc.morph_deltas;
  let weights = (float*)pc.morph_weights;

  var p = v.position;
  var n = v.normal;
  for (uint t = 0u; t < pc.morph_count; ++t) {
    let w = weights[t];
    if (abs(w) < 1e-5f) {
      continue;
    }
    let d = deltas[t * pc.vertex_count + i];
    p += w * d.position.xyz;
    n += w * d.normal.xyz;
  }

  let len2 = dot(n, n);
  v.position = p;
  v.normal   = (len2 > 1e-12f) ? n * rsqrt(len2) : v.normal;
}

// ----------------------------------------------------------------------------
// Linear blend skinning.

void skinLinear(SkinVertex sv, uint last, Vertex v, inout Vertex o) {
  float4x4 m = float4x4(
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f
  );

  if (hasInfluence(sv)) {
    let palette = (float4x4*)pc.palette;
    m = float4x4(0.0f);
    m += sv.weights.x * palette[min(sv.joints.x, last)];
    m += sv.weights.y * palette[min(sv.joints.y, last)];
    m += sv.weights.z * palette[min(sv.joints.z, last)];
    m += sv.weights.w * palette[min(sv.joints.w, last)];
  }

  o.position = mul(m, float4(v.position, 1.0f)).xyz;
  o.normal   = transformNormal((float3x3)m, v.normal);
  o.tangent  = float4(normalize(mul(m, float4(v.tangent.xyz, 0.0f)).xyz), v.tangent.w);
}

// ----------------------------------------------------------------------------
// Dual quaternion linear blending (Kavan et al. 2008).

void skinDualQuaternion(SkinVertex sv, uint last, Vertex v, inout Vertex o) {
  let dq = (float4*)pc.palette;
  let j  = min(sv.joints, uint4(last));

  let r0 = dq[4u * j.x];
  let r1 = dq[4u * j.y];
  let r2 = dq[4u * j.z];
  let r3 = dq[4u * j.w];

  // Antipodality : put every quaternion in the hemisphere of the first influence.
  var w = sv.weights;
  w.y *= (dot(r0, r1) < 0.0f) ? -1.0f : 1.0f;
  w.z *= (dot(r0, r2) < 0.0f) ? -1.0f : 1.0f;
  w.w *= (dot(r0, r3) < 0.0f) ? -1.0f : 1.0f;

  var A = w.x * r0 + w.y * r1 + w.z * r2 + w.w * r3;
  var B = w.x * dq[4u * j.x + 1u]
        + w.y * dq[4u * j.y + 1u]
        + w.z * dq[4u * j.z + 1u]
        + w.w * dq[4u * j.w + 1u];

  // Normalize.
  let len2 = dot(A, A);
  if (len2 > 1e-12f) {
    let inv = rsqrt(len2);
    A *= inv;
    B *= inv;
  } else {
    A = float4(0.0f, 0.0f, 0.0f, 1.0f);
    B = float4(0.0f);
  }

  let translation = 2.0f * (A.w * B.xyz - B.w * A.xyz + cross(A.xyz, B.xyz));

  o.position = rotateByQuat(A, v.position) + translation;
  o.normal   = normalize(rotateByQuat(A, v.normal));
  o.tangent  = float4(normalize(rotateByQuat(A, v.tangent.xyz)), v.tangent.w);
}

// ----------------------------------------------------------------------------

[shader("compute")]
[numthreads(kSkinningGroupSize, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID) {
  let i = tid.x;
  if (i >= pc.vertex_count) {
    return;
  }

  let src = (Vertex*)pc.src_vertices;
  var dst = (Vertex*)pc.dst_vertices;

  Vertex v = src[i];
  if (pc.morph_count > 0u) {
    applyMorph(v, i);
  }
  Vertex o = v;  // (morph only : tangents are kept)

  if (pc.mode != kSkinningModeNone) {
    let skin = (SkinVertex*)pc.skin_vertices;
    let sv   = skin[i];
    let last = max(pc.joint_count, 1u) - 1u;

    if (pc.mode == kSkinningModeDualQuaternion && hasInfluence(sv)) {
      skinDualQuaternion(sv, last, v, o);
    } else {
      skinLinear(sv, last, v, o);
    }
  }

  dst[i] = o;
}

/* -------------------------------------------------------------------------- */