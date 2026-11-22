#ifndef AER_RENDERER_RENDER_FLAGS_H_
#define AER_RENDERER_RENDER_FLAGS_H_

#include "aer/shaders/material/renderer_flags.h"

/* Tonemapping operators.
 * (the values must match the TONEMAPPING_* defines of shaders/shared/tonemapping.glsl) */
enum class Tonemapping : uint32_t {
  None            = 0,
  Linear          = 1,
  SimpleReinhard  = 2,
  LumaReinhard    = 3,
  LumaReinhard2   = 4,
  RomBinDaHouse   = 5,
  Filmic          = 6,  //< (gamma is baked in the curve)
  FilmicACES      = 7,
  Uncharted2      = 8,
  PBRNeutral      = 9,
};

#endif // AER_RENDERER_RENDER_FLAGS_H_