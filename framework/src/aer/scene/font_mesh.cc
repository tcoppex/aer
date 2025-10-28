#include "aer/scene/font_mesh.h"

namespace scene {

/* -------------------------------------------------------------------------- */

bool FontMesh::generate(
  Font const& font,
  float extrusionDepth,
  uint32_t extrusionSampleCount
) {
  reset();
  font_ptr_ = &font;

  for (auto const& [ucode, glyph] : font.glyph_map()) {
    auto glyph_info = GlyphInfo{
      .primitive_index = primitive_count(),
    };
    bool success = Path2D::BuildShapeMesh(
      glyph.path, *this, extrusionDepth, extrusionSampleCount
    );
    if (!success && !glyph.path.polylines().empty()) {
      return false;
    }

    glyph_info.count = primitive_count() - glyph_info.primitive_index;
    glyph_info_map_[ucode] = glyph_info;
  }

  return true;
}

// -----------------------------------------------------------------------------

[[nodiscard]]
FontMesh::TextDrawInfo FontMesh::buildTextDrawInfo(
  std::u16string const& text,
  bool enableKerning
) const {
  LOG_CHECK(font_ptr_ != nullptr);

  FontMesh::TextDrawInfo result;

  result.glyphs.resize(text.size());

  int advance = 0;
  for (size_t i = 0; i < text.size(); ++i) {
    auto const ucode = text[i];
    if (ucode == 0) {
      break;
    }
    if (!font_ptr_->hasGlyph(ucode)) {
      continue;
    }
    auto const glyph = font_ptr_->findGlyph(ucode);

    int kernAdvance = 0;
    if (enableKerning && (i > 0)) {
      auto ucode_prev = text[i-1];
      kernAdvance = font_ptr_->kern_advance(ucode_prev, ucode);
    }

    advance += kernAdvance;
    float const tx = static_cast<float>(advance + glyph.leftSideBearing);
    advance += glyph.advanceWidth;

    result.glyphs[i] = {
      .code = ucode,
      .matrix = linalg::translation_matrix(vec3(tx, 0, 0)),
      .submeshes = glyph_submeshes(ucode),
    };
  }
  result.cx -= advance / 2;

  return result;
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"
