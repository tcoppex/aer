#version 460
#extension GL_GOOGLE_include_directive : require

// ----------------------------------------------------------------------------
/*
  Apply a Sobel convolution to normal and depth values.

  Ref: https://www.cs.princeton.edu/courses/archive/fall00/cs597b/papers/saito90.pdf
*/
// ----------------------------------------------------------------------------


#include <shared/maths.glsl>
#include <shared/linearize_depth.glsl>

// ----------------------------------------------------------------------------

layout(set = 0, binding = 0) uniform sampler2D iChannels[]; // XY Normal + Z Depth + W ObjectID

layout(set = 0, binding = 1) buffer zValues {
  int minmax[2];
};

layout(push_constant) uniform params_ {
  float normal_edge_dx;
  float depth_edge_dx;
};

// ----------------------------------------------------------------------------

layout(location = 0) in vec2 fragCoord;

layout(location = 0) out float fragColor;

// ----------------------------------------------------------------------------

float calculateDepthNormalGradient(ivec2 texelCoord, float zNear, float zFar) {
  vec4 X = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(0, 0));

  int objId = floatBitsToInt(X.w);

  if ( (X.z < 0.0001) || (objId == 0)) {
    return 0;
  }

  // ----------------------------
  // Normal Gradient.

  vec4 A = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(-1.0, +1.0));
  vec4 B = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(+0.0, +1.0));
  vec4 C = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(+1.0, +1.0));
  vec4 D = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(-1.0, +0.0));
  vec4 E = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(+1.0, +0.0));
  vec4 F = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(-1.0, -1.0));
  vec4 G = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(+0.0, -1.0));
  vec4 H = texelFetchOffset(iChannels[0], texelCoord, 0, ivec2(+1.0, -1.0));

  vec3 An = decodeNormal(A.xy);
  vec3 Bn = decodeNormal(B.xy);
  vec3 Cn = decodeNormal(C.xy);
  vec3 Dn = decodeNormal(D.xy);
  vec3 Xn = decodeNormal(X.xy);
  vec3 En = decodeNormal(E.xy);
  vec3 Fn = decodeNormal(F.xy);
  vec3 Gn = decodeNormal(G.xy);
  vec3 Hn = decodeNormal(H.xy);

  float normal_gradient = 0.0f;
  {
    // compute length of gradient using Sobel/Kroon operator
    const float k0     = 17.f / 23.75;
    const float k1     = 61.f / 23.75;
    const vec3  grad_y = k0 * An + k1 * Bn + k0 * Cn - k0 * Fn - k1 * Gn - k0 * Hn;
    const vec3  grad_x = k0 * Cn + k1 * En + k0 * Hn - k0 * An - k1 * Dn - k0 * Fn;
    const float g      = length(grad_x) + length(grad_y);

    normal_gradient = smoothstep(2.0f, 3.0f, g * normal_edge_dx);
  }

  // ----------------------------
  // Depth Gradient.

  float depth_gradient = 0.0f;
#if 0
  {
    float inv_zNear = 1.0 / zNear;
    float inv_zFar = 1.0 / zFar;
    A.z = normalizedPerspectiveDepth(A.z, inv_zNear, inv_zFar);
    B.z = normalizedPerspectiveDepth(B.z, inv_zNear, inv_zFar);
    C.z = normalizedPerspectiveDepth(C.z, inv_zNear, inv_zFar);
    D.z = normalizedPerspectiveDepth(D.z, inv_zNear, inv_zFar);
    E.z = normalizedPerspectiveDepth(E.z, inv_zNear, inv_zFar);
    F.z = normalizedPerspectiveDepth(F.z, inv_zNear, inv_zFar);
    G.z = normalizedPerspectiveDepth(G.z, inv_zNear, inv_zFar);
    H.z = normalizedPerspectiveDepth(H.z, inv_zNear, inv_zFar);
    X.z = normalizedPerspectiveDepth(X.z, inv_zNear, inv_zFar);

    float g = ( abs(A.z + 2 * B.z + C.z - F.z - 2 * G.z - H.z)
              + abs(C.z + 2 * E.z + H.z - A.z - 2 * D.z - F.z)) / 8.0;
    float l = (8 * X.z - A.z - B.z - C.z - D.z - E.z - F.z - G.z - H.z) / 3.0;

    depth_gradient = (l + g) * depth_edge_dx;
    depth_gradient = smoothstep(0.03f, 0.1f, depth_gradient);
  }
#endif

  return normal_gradient + depth_gradient;
}

// ----------------------------------------------------------------------------

void main() {
  ivec2 size = textureSize(iChannels[0], 0);
  ivec2 texelCoord = ivec2(vec2(size) * fragCoord.st);

  float zNear = intBitsToFloat(minmax[0]);
  float zFar  = intBitsToFloat(minmax[1]);

  fragColor = calculateDepthNormalGradient(texelCoord, zNear, zFar);
}

// ----------------------------------------------------------------------------
