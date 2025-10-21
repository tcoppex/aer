#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC

#include "aer/scene/font.h"

namespace scene {

/* -------------------------------------------------------------------------- */

const std::u16string Font::kDefaultChars = std::u16string(
  u" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~àéçùïôè"
);

// ----------------------------------------------------------------------------

bool Font::load(std::string_view filename) {
  auto const fullpath = std::string(ASSETS_DIR "fonts/") + filename.data();

  if (!file_reader_.read(fullpath)) {
    return false;
  }

  auto const* buffer = file_reader_.buffer.data();
  auto const font_offset_for_index = stbtt_GetFontOffsetForIndex(buffer, 0);

  if (!stbtt_InitFont(&font_, buffer, font_offset_for_index)) {
    return false;
  }
  return true;
}

// ----------------------------------------------------------------------------

void Font::generate(
  std::u16string const& corpus,
  float const curve_resolution
) {
  for (auto const& c : corpus) {
    Glyph glyph{
      .index = stbtt_FindGlyphIndex(&font_, c),
    };

    stbtt_vertex *glyph_verts{};
    auto const nvertices = stbtt_GetGlyphShape(&font_, glyph.index, &glyph_verts);

    for (int i = 0; i < nvertices; ++i) {
      auto const& v = glyph_verts[i];
      auto const pt = vec2(v.x, v.y);

      if (v.type == STBTT_vmove) {
        glyph.path.moveTo(pt);
      } else if (v.type == STBTT_vline) {
        glyph.path.lineTo(pt);
      } else if (v.type == STBTT_vcurve) {
        glyph.path.quadBezierTo(vec2(v.cx, v.cy), pt, curve_resolution);
      }
    }
    stbtt_FreeShape(&font_, glyph_verts);
    glyph_map_[c] = glyph;
  }
}

// ----------------------------------------------------------------------------

void Font::writeAscii(std::u16string const& msg, int y_size) const {
  int const NN = 64;
  unsigned char *bitmaps[NN];
  int ws[NN];
  int hs[NN];
  int max_h = 0;

  auto const msglen = std::min((size_t)NN, msg.length());

  for (size_t k = 0; k < msglen; ++k) {
    auto c = msg[k];
    bitmaps[k] = stbtt_GetCodepointBitmap(
      &font_, 0, pixelScaleFromSize(y_size), c, &ws[k], &hs[k], 0, 0
    );
    max_h = hs[k] > max_h ? hs[k] : max_h;
  }

  for (int j = 0; j < max_h; ++j) {
    for (size_t k = 0; k < msglen; ++k) {
      int jj = j;
      if (j < max_h - hs[k]) {
        break;
      }
      if (max_h != hs[k]) {
        jj = j-max_h+hs[k];
      }
      for (int i=0; i < ws[k]; ++i) {
        putchar(" .:ioVM@"[bitmaps[k][(jj)*ws[k]+i] >> 5]);
      }
    }
    putchar('\n');
  }
}

// ----------------------------------------------------------------------------

float Font::pixelScaleFromSize(int fontsize) const noexcept {
  return stbtt_ScaleForPixelHeight(&font_, fontsize);
}

/* -------------------------------------------------------------------------- */

}