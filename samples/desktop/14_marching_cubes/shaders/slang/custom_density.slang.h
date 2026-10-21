
// import "density_common.slang";
#include "sdf_shared.slang.h" //

// ----------------------------------------------------------------------------
//
// Procedural Terrain
//
// ----------------------------------------------------------------------------

// Ground.
static const float kGroundLevel       = 8.0;

// Relief (3 + 3 + 2 octaves of 3D noise).
static const float kReliefFreq        = 0.0065;   // base frequency (1 / world unit)
static const float kReliefAmp         = 36.0;     // base amplitude (world unit)
static const float kDetailFreq        = 0.052;
static const float kDetailAmp         = 6.5;
static const float kGrainFreq         = 0.31;
static const float kGrainAmp          = 0.9;
static const float kVerticalSquash    = 0.75;

// Domain warp.
static const float kWarpFreq          = 0.006;
static const float3 kWarpAmp          = float3(24.0, 9.0, 24.0);

// Terraces.
static const float kTerraceHeight     = 13.0;
static const float kTerraceStrength   = 0.7;

// Canyons.
static const float kCanyonFreq        = 0.0036;
static const float kCanyonWidth       = 0.085;
static const float kCanyonDepth       = 26.0;

// Pillars.
static const float kPillarCell        = 96.0;
static const float kPillarChance      = 0.42;
static const float kPillarHalfHeight  = 44.0;

// Caves & floor.
static const float kCaveFreq          = 0.022;
static const float kCaveRadius        = 4.5;
static const float kFloorLevel        = -30.0;

// ----------------------------------------------------------------------------

float sdPillars(float3 p) {
  let cell = floor(p.xz / kPillarCell);
  let h    = float3(pcg3d(uint3(int3(int(cell.x), 7, int(cell.y))))) * (1.0f / 4294967295.0f);

  if (h.x > kPillarChance) {
    return 1.0e3;
  }

  // (kept away from the cell borders so no neighbour lookup is needed)
  let center = (cell + 0.3 + 0.4 * h.yz) * kPillarCell;
  let height = kPillarHalfHeight * (0.45 + 0.55 * h.z);
  let radius = (3.0 + 4.0 * h.y) * (1.0 + 0.35 * noise3(p * 0.12));

  let local  = float3(p.x - center.x, p.y - kGroundLevel + 24.0 - height, p.z - center.y);
  // Flared base (the bottom is buried 24 units under the ground).
  let flare  = 1.0 + 1.6 * exp(-max(p.y - kGroundLevel + 24.0, 0.0) * 0.06);
  return sdCappedCylinder(local, height, radius * flare) * 0.7;
}

// ----------------------------------------------------------------------------

float sdTerrain(float3 p) {
  // Domain warp.
  let wp = p * kWarpFreq;
  let warp = float3(
    fbm3(wp + float3( 3.1, 7.7, 1.3), 2),
    fbm3(wp + float3( 9.2, 1.4, 5.5), 2),
    fbm3(wp + float3( 5.5, 2.2, 8.1), 2)
  );

  var q = p + warp * kWarpAmp;
  {
    let mask = smoothstep(-0.1, 0.35, fbm3(float3(q.x, 1.7, q.z) * 0.004, 2));
    let ty   = q.y / kTerraceHeight;
    let step = kTerraceHeight * (floor(ty) + smoothstep(0.62, 0.95, frac(ty)));
    q.y = lerp(q.y, step, kTerraceStrength * mask);
  }

  let s = float3(1.0, kVerticalSquash, 1.0);
  let relief = kReliefAmp * fbm3(q * s * kReliefFreq, 3)
             + kDetailAmp * fbm3(q * s * kDetailFreq, 2)
             + kGrainAmp  * noise3(q * kGrainFreq);

  var d = q.y - kGroundLevel - relief;
  {
    let n      = fbm3(float3(q.x, 5.5, q.z) * kCanyonFreq, 3);
    let canyon = 1.0 - smoothstep(0.0, kCanyonWidth, abs(n));
    d += kCanyonDepth * canyon * smoothstep(-0.2, 0.3, relief / kReliefAmp + 0.2);
  }

  d = opSmoothUnion(d, sdPillars(p), 6.0);
  d -= saturate((kFloorLevel - p.y) / 20.0) * 80.0;

  {
    let cq   = p * kCaveFreq;
    let tube = length(float2(
      fbm3(cq + float3(17.0, 3.0, 29.0), 2),
      fbm3(cq + float3(71.0, 41.0, 5.0), 2)
    )) * 45.0;

    var cave = tube - kCaveRadius - 1.6 * noise3(p * 0.11);
    cave    += (1.0 - smoothstep(-62.0, -44.0, p.y)) * 30.0;
    d = opSmoothSubtraction(d, cave, 2.5);
  }

  return d;
}

// ----------------------------------------------------------------------------

float computeDensity(float3 ws) {
  return sdTerrain(ws);
}

// ----------------------------------------------------------------------------
