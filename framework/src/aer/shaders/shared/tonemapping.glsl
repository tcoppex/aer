#ifndef SHADERS_SHARED_TONEMAPPING_GLSL_
#define SHADERS_SHARED_TONEMAPPING_GLSL_

// ----------------------------------------------------------------------------
/*
  Tonemapping operators.

  Adapted from https://github.com/worleydl/hdr-shaders

  References :
    https://gdcvault.com/play/1012351/Uncharted-2-HDR
    http://slideshare.net/ozlael/hable-john-uncharted2-hdr-lighting
    http://filmicworlds.com/blog/filmic-tonemapping-operators/
    http://filmicworlds.com/blog/why-reinhard-desaturates-your-blacks/
    http://filmicworlds.com/blog/why-a-filmic-curve-saturates-your-blacks/
    http://imdoingitwrong.wordpress.com/2010/08/19/why-reinhard-desaturates-my-blacks-3/
    http://mynameismjp.wordpress.com/2010/04/30/a-closer-look-at-tone-mapping/
    http://renderwonk.com/publications/s2010-color-course/
    https://github.com/KhronosGroup/ToneMapping/
*/
// ----------------------------------------------------------------------------

#define TONEMAPPING_NONE              0
#define TONEMAPPING_LINEAR            1
#define TONEMAPPING_SIMPLE_REINHARD   2
#define TONEMAPPING_LUMA_REINHARD     3
#define TONEMAPPING_LUMA_REINHARD_2   4
#define TONEMAPPING_ROMBINDAHOUSE     5
#define TONEMAPPING_FILMIC            6
#define TONEMAPPING_FILMIC_ACES       7
#define TONEMAPPING_UNCHARTED         8
#define TONEMAPPING_PBR_NEUTRAL       9

// ----------------------------------------------------------------------------

float gammaCorrection(in float x, float gamma) {
  return pow(x, 1.0 / gamma);
}

vec3 gammaCorrection(in vec3 rgb, float gamma) {
  return pow(rgb, vec3(1.0 / gamma));
}

float sRGBToLinear(in float x, float gamma) {
  return pow(x, gamma);
}

vec3 sRGBToLinear(in vec3 rgb, float gamma) {
  return pow(rgb, vec3(gamma));
}

// ----------------------------------------------------------------------------

vec3 linearToneMapping(vec3 color, float exposure, float gamma) {
  color = clamp(exposure * color, 0.0, 1.0);
  color = gammaCorrection(color, gamma);
  return color;
}

vec3 simpleReinhardToneMapping(vec3 color, float exposure, float gamma) {
  color *= exposure / (1.0 + color / exposure);
  color = gammaCorrection(color, gamma);
  return color;
}

vec3 lumaBasedReinhardToneMapping(vec3 color, float gamma) {
  float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
  float toneMappedLuma = luma / (1.0 + luma);
  color *= toneMappedLuma / max(luma, 1e-6); // (avoid 0/0 on black pixels)
  color = gammaCorrection(color, gamma);
  return color;
}

vec3 whitePreservingLumaBasedReinhardToneMapping(vec3 color, float gamma) {
  const float white = 2.0;
  const float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
  const float toneMappedLuma = luma * (1.0 + luma / (white*white)) / (1.0 + luma);
  color *= toneMappedLuma / max(luma, 1e-6); // (avoid 0/0 on black pixels)
  color = gammaCorrection(color, gamma);
  return color;
}

vec3 romBinDaHouseToneMapping(vec3 color, float gamma) {
  color = exp( -1.0 / ( 2.72 * color + 0.15 ) );
  color = gammaCorrection(color, gamma);
  return color;
}

vec3 filmicToneMapping(vec3 color) {
  color = max(vec3(0.0), color - vec3(0.004));
  color = (color * (6.2 * color + 0.5)) / (color * (6.2 * color + 1.7) + 0.06);
  return color;
}

vec3 filmicACESToneMapping(vec3 color, float gamma) {
  const float A = 2.51;
  const float B = 0.03;
  const float C = 2.43;
  const float D = 0.59;
  const float E = 0.14;
  return gammaCorrection(clamp((color * (A * color + B)) / (color * (C * color + D) + E), 0.0, 1.0), gamma);
}

vec3 uncharted2ToneMapping(vec3 color, float exposure, float gamma) {
  const float A = 0.15; // Shoulder Strength
  const float B = 0.50; // Linear Strength
  const float C = 0.10; // Linear Angle
  const float D = 0.20; // Toe Strength
  const float E = 0.02; // Toe Numerator
  const float F = 0.30; // Toe Denominator
  const float W = 11.2; // Linear White Point value
  // const float exposure = 2.0;

  color *= exposure;
  color = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;

  const float white = ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;
  color /= white;

  color = gammaCorrection(color, gamma);

  return color;
}

// Input color is non-negative and resides in the Linear Rec. 709 color space.
// Output color is also Linear Rec. 709, but in the [0, 1] range.
vec3 pbrNeutralToneMapping( vec3 color ) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;

  float x = min(color.r, min(color.g, color.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  color -= offset;

  float peak = max(color.r, max(color.g, color.b));
  if (peak < startCompression) return color;

  const float d = 1. - startCompression;
  float newPeak = 1. - d * d / (peak + d - startCompression);
  color *= newPeak / peak;

  float g = 1. - 1. / (desaturation * (peak - newPeak) + 1.);
  return mix(color, newPeak * vec3(1, 1, 1), g);
}

// ----------------------------------------------------------------------------

vec3 toneMapping(int mode, in vec3 color, float exposure, float gamma) {
  switch (mode) {
    case TONEMAPPING_LINEAR:
      return linearToneMapping(color, exposure, gamma);

    case TONEMAPPING_SIMPLE_REINHARD:
      return simpleReinhardToneMapping(color, exposure, gamma);

    case TONEMAPPING_LUMA_REINHARD:
      return lumaBasedReinhardToneMapping(color, gamma);

    case TONEMAPPING_LUMA_REINHARD_2:
      return whitePreservingLumaBasedReinhardToneMapping(color, gamma);

    case TONEMAPPING_ROMBINDAHOUSE:
      return romBinDaHouseToneMapping(color, gamma);

    case TONEMAPPING_FILMIC:
      return filmicToneMapping(color);

    case TONEMAPPING_FILMIC_ACES:
      return filmicACESToneMapping(color, gamma);

    case TONEMAPPING_UNCHARTED:
      return uncharted2ToneMapping(color, exposure, gamma);

    case TONEMAPPING_PBR_NEUTRAL:
      return pbrNeutralToneMapping(color);

    default:
    case TONEMAPPING_NONE:
      return color;
  }

  return color;
}

// ----------------------------------------------------------------------------

/*
 * Apply an exposure and a tonemapping operator to a linear HDR color.
 *
 * The result is *linear* and display-referred, in [0, 1] : the sRGB encoding
 * is left to the swapchain (the final blit targets an *_SRGB image), so no
 * gamma is applied here.
 *
 * Note: TONEMAPPING_FILMIC has its gamma baked in its curve, it should not be
 *       used with this path.
 */
vec3 tonemapDisplayLinear(in vec3 hdr, float exposure, int mode) {
  return toneMapping(mode, hdr * exposure, 1.0, 1.0);
}

// ----------------------------------------------------------------------------

#endif // SHADERS_SHARED_TONEMAPPING_GLSL_
