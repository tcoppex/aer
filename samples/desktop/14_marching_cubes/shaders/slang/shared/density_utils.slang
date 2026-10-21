// ----------------------------------------------------------------------------
//
// Signed-distance / density toolkit : noise, operators and primitives.
//
// ----------------------------------------------------------------------------

module density_utils;

// ----------------------------------------------------------------------------

#define PUBLIC public

#if 0
// import distance_function_helpers.slang;
// import simplex_perlin_nois.slang;

// // User defined distance function
// float computeDensity(float3 ws);

// float3 computeDensityNormal(float3 p)
// {
//   float eps = 0.001;
//   float3 ex = float3(eps, 0.0, 0.0);
//   float3 ey = float3(0.0, eps, 0.0);
//   float3 ez = float3(0.0, 0.0, eps);

//   float nx = computeDensity(p + ex) - computeDensity(p - ex);
//   float ny = computeDensity(p + ey) - computeDensity(p - ey);
//   float nz = computeDensity(p + ez) - computeDensity(p - ez);

//   return normalize(float3(nx, ny, nz));
// }
#endif

// ----------------------------------------------------------------------------
// Noise
// ----------------------------------------------------------------------------

PUBLIC
uint3 pcg3d(uint3 v) {
  v = v * 1664525u + 1013904223u;
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  v ^= (v >> 16u);
  v.x += v.y * v.z;
  v.y += v.z * v.x;
  v.z += v.x * v.y;
  return v;
}

PUBLIC
float3 gradientAt(int3 lattice) {
  let h = pcg3d(uint3(lattice));
  return float3(h) * (2.0f / 4294967295.0f) - 1.0;
}

// Gradient noise (Perlin, quintic fade), range ~[-0.8, 0.8].
PUBLIC
float noise3(float3 p) {
  let i = floor(p);
  let f = p - i;
  let u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  let c = int3(i);

  let g000 = dot(gradientAt(c + int3(0, 0, 0)), f - float3(0, 0, 0));
  let g100 = dot(gradientAt(c + int3(1, 0, 0)), f - float3(1, 0, 0));
  let g010 = dot(gradientAt(c + int3(0, 1, 0)), f - float3(0, 1, 0));
  let g110 = dot(gradientAt(c + int3(1, 1, 0)), f - float3(1, 1, 0));
  let g001 = dot(gradientAt(c + int3(0, 0, 1)), f - float3(0, 0, 1));
  let g101 = dot(gradientAt(c + int3(1, 0, 1)), f - float3(1, 0, 1));
  let g011 = dot(gradientAt(c + int3(0, 1, 1)), f - float3(0, 1, 1));
  let g111 = dot(gradientAt(c + int3(1, 1, 1)), f - float3(1, 1, 1));

  return lerp(
    lerp(lerp(g000, g100, u.x), lerp(g010, g110, u.x), u.y),
    lerp(lerp(g001, g101, u.x), lerp(g011, g111, u.x), u.y),
    u.z
  );
}

static const float3x3 kFbmRotation = float3x3(
   0.00,  0.80,  0.60,
  -0.80,  0.36, -0.48,
  -0.60, -0.48,  0.64
);

// Fractal sum, the lattice is rotated at each octave to hide the grid axes.
PUBLIC
float fbm3(float3 p, int octaves) {
  var sum = 0.0;
  var amp = 0.5;
  var q   = p;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * noise3(q);
    q = mul(kFbmRotation, q) * 2.03 + float3(17.1, 9.7, 31.3);
    amp *= 0.5;
  }
  return sum * 2.0; // ~[-1, 1]
}

// ----------------------------------------------------------------------------
// Operators
// ----------------------------------------------------------------------------

PUBLIC float opUnion(float a, float b) {
  return min(a, b);
}

PUBLIC
float opIntersection(float a, float b) {
  return max(a, b);
}

PUBLIC
float opSubtraction(float a, float b) {
  return max(a, -b);
}

// Polynomial smooth-min : no exp(), safe for any magnitude.
PUBLIC
float opSmoothUnion(float a, float b, float k) {
  let h = max(k - abs(a - b), 0.0) / k;
  return min(a, b) - h * h * k * 0.25;
}

PUBLIC
float opSmoothSubtraction(float a, float b, float k) {
  let h = max(k - abs(a + b), 0.0) / k;
  return max(a, -b) + h * h * k * 0.25;
}

PUBLIC
float3 opRepeat(float3 p, float3 c) {
  return p - c * round(p / c);
}

PUBLIC
float3x3 rotateAxisAngle(float3 _axis, float angle) {
  let axis = normalize(_axis);
  let c = cos(angle);
  let s = sin(angle);
  let t = 1.0 - c;

  return float3x3(
    t * axis.x * axis.x + c,           t * axis.x * axis.y - s * axis.z,  t * axis.x * axis.z + s * axis.y,
    t * axis.x * axis.y + s * axis.z,  t * axis.y * axis.y + c,           t * axis.y * axis.z - s * axis.x,
    t * axis.x * axis.z - s * axis.y,  t * axis.y * axis.z + s * axis.x,  t * axis.z * axis.z + c
  );
}

// ----------------------------------------------------------------------------
// Primitives
// ----------------------------------------------------------------------------

PUBLIC
float sdPlane(float3 p, float4 n) {
  return n.w + dot(p, n.xyz);
}

PUBLIC
float sdSphere(float3 p, float r) {
  return length(p) - r;
}

PUBLIC
float sdRoundBox(float3 p, float3 b, float r) {
  let q = abs(p) - b;
  return length(max(q, float3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

PUBLIC
float sdTorus(float3 p, float2 t) {
  let q = float2(length(p.xz) - t.x, p.y);
  return length(q) - t.y;
}

PUBLIC
float sdCappedCylinder(float3 p, float h, float r) {
  let d = abs(float2(length(p.xz), p.y)) - float2(r, h);
  return min(max(d.x, d.y), 0.0) + length(max(d, float2(0.0)));
}

// ----------------------------------------------------------------------------
