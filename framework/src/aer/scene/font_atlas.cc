#include "aer/scene/font_atlas.h"

#include <msdfgen.h>

#if !defined(NDEBUG)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_STATIC
#include <stb/stb_image_write.h>
#endif

namespace scene {

/* -------------------------------------------------------------------------- */

namespace {

/* Convert a stb outline (font units, Y-up) to an msdfgen::Shape in em units. */
msdfgen::Shape BuildShape(std::vector<stbtt_vertex> const& verts, double s) {
  using msdfgen::Point2;

  msdfgen::Shape shape;
  msdfgen::Contour* contour = nullptr;
  Point2 start{}, cur{};

  auto close_contour = [&]() {
    if (contour && (cur.x != start.x || cur.y != start.y)) {
      contour->addEdge(msdfgen::EdgeHolder(cur, start));
    }
  };

  for (auto const& v : verts) {
    Point2 const p(v.x * s, v.y * s);
    switch (v.type) {
      case STBTT_vmove:
        close_contour();
        contour = &shape.addContour();
        start = cur = p;
        break;
      case STBTT_vline:
        if (contour) contour->addEdge(msdfgen::EdgeHolder(cur, p));
        cur = p;
        break;
      case STBTT_vcurve:
        if (contour) {
          contour->addEdge(
            msdfgen::EdgeHolder(cur, Point2(v.cx * s, v.cy * s), p)
          );
        }
        cur = p;
        break;
      case STBTT_vcubic:
        if (contour) {
          contour->addEdge(msdfgen::EdgeHolder(
            cur, Point2(v.cx * s, v.cy * s), Point2(v.cx1 * s, v.cy1 * s), p
          ));
        }
        cur = p;
        break;
      default:
        break;
    }
  }
  close_contour();

  shape.normalize();
  msdfgen::edgeColoringSimple(shape, 3.0);  // 3.0 = corner angle threshold
  return shape;
}

inline uint8_t ToByte(float x) {
  return static_cast<uint8_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct Cell {
  char16_t code{};
  msdfgen::Shape shape{};
  double l{}, b{}, r{}, t{};  // shape bounds (em)
  uint32_t w{}, h{};          // cell size in pixels (padding included)
  uint32_t x{}, y{};          // position in the atlas
};

} // namespace

/* -------------------------------------------------------------------------- */

bool FontAtlas::generate(
  Font const& font,
  std::u16string const& corpus,
  Params const& params
) {
  params_ = params;
  glyphs_.clear();

  double const em_scale = font.emScale();
  double const ppem = params_.pixelsPerEm;
  double const range_em = params_.pxRange / ppem;

  auto const pad = static_cast<uint32_t>(std::ceil(params_.pxRange)) + 1u;

  // 1. Build shapes and cell sizes.
  std::vector<Cell> cells;
  for (auto const c : corpus) {
    if (glyphs_.contains(c)) {
      continue;
    }

    GlyphInfo info{.code = c};
    if (font.hasGlyph(c)) {
      info.advance = static_cast<float>(font.findGlyph(c).advanceWidth * em_scale);
    }

    auto const verts = font.getGlyphShape(c);
    if (verts.empty()) {
      glyphs_[c] = info;
      continue;
    }

    Cell cell{.code = c, .shape = BuildShape(verts, em_scale)};
    cell.shape.bound(cell.l, cell.b, cell.r, cell.t);
    cell.w = static_cast<uint32_t>(std::ceil((cell.r - cell.l) * ppem)) + 2u * pad;
    cell.h = static_cast<uint32_t>(std::ceil((cell.t - cell.b) * ppem)) + 2u * pad;
    info.visible = true;
    glyphs_[c] = info;
    cells.push_back(std::move(cell));
  }

  // 2. Shelf packing (tallest first).
  std::sort(cells.begin(), cells.end(), [](Cell const& a, Cell const& b) {
    return a.h > b.h;
  });

  width_ = params_.atlasWidth;
  uint32_t cx = 0, cy = 0, shelf_h = 0;
  for (auto& cell : cells) {
    if (cell.w > width_) {
      LOGW("glyph cell wider than the atlas, increase atlasWidth");
      return false;
    }
    if (cx + cell.w > width_) {
      cy += shelf_h;
      cx = 0;
      shelf_h = 0;
    }
    cell.x = cx;
    cell.y = cy;
    cx += cell.w;
    shelf_h = std::max(shelf_h, cell.h);
  }
  height_ = std::max(1u, cy + shelf_h);

  pixels_.assign(static_cast<size_t>(width_) * height_ * 4u, 0u);

  // 3. Generate MTSDF per cell, copy (and flip rows) in the atlas.
  for (auto const& cell : cells) {
    msdfgen::Bitmap<float, 4> bmp(cell.w, cell.h);

    msdfgen::Vector2 const translate(
      -cell.l + pad / ppem,
      -cell.b + pad / ppem
    );
    msdfgen::SDFTransformation const transform(
      msdfgen::Projection(msdfgen::Vector2(ppem), translate),
      msdfgen::Range(range_em)
    );
    msdfgen::generateMTSDF(bmp, cell.shape, transform);

    for (uint32_t y = 0; y < cell.h; ++y) {
      // msdfgen row 0 = bottom => atlas row (cell.y + h-1-y).
      uint32_t const dst_row = cell.y + (cell.h - 1u - y);
      auto* dst = &pixels_[(static_cast<size_t>(dst_row) * width_ + cell.x) * 4u];
      for (uint32_t x = 0; x < cell.w; ++x) {
        float const* src = bmp(x, y);
        for (int k = 0; k < 4; ++k) {
          dst[x * 4u + k] = ToByte(src[k]);
        }
      }
    }

    auto& info = glyphs_[cell.code];
    double const pad_em = pad / ppem;
    info.planeL = static_cast<float>(cell.l - pad_em);
    info.planeR = static_cast<float>(cell.r + pad_em);
    info.planeB = static_cast<float>(cell.b - pad_em);
    info.planeT = static_cast<float>(cell.t + pad_em);
    info.texcoord = {
      static_cast<float>(cell.x) / width_,
      static_cast<float>(cell.x + cell.w) / width_,
      static_cast<float>(cell.y) / height_,
      static_cast<float>(cell.y + cell.h) / height_,
    };
  }

  LOGD("FontAtlas: {} glyphs, {}x{}", cells.size(), width_, height_);
  return true;
}

// ----------------------------------------------------------------------------

FontAtlas::TextLayout FontAtlas::layout(
  Font const& font,
  std::u16string const& text,
  bool kerning
) const {
  TextLayout out{};
  float const em_scale = static_cast<float>(font.emScale());
  float pen = 0.0f;

  for (size_t i = 0; i < text.size(); ++i) {
    auto const* g = findGlyph(text[i]);
    if (!g) {
      continue;
    }
    if (g->visible) {
      out.quads.push_back({
        .position = vec2(pen + g->planeL, g->planeB),
        .size = vec2(g->planeR - g->planeL, g->planeT - g->planeB),
        .texcoord = g->texcoord,
      });
    }
    pen += g->advance;
    if (kerning && (i + 1 < text.size())) {
      pen += font.getGlyphKernAdvance(text[i], text[i + 1]) * em_scale;
    }
  }
  out.width = pen;
  return out;
}

// ----------------------------------------------------------------------------

bool FontAtlas::writePNG(std::string const& filename) const {
  if (pixels_.empty()) {
    return false;
  }

#if defined(INCLUDE_STB_IMAGE_WRITE_H)
  return stbi_write_png(
    filename.c_str(), width_, height_, 4, pixels_.data(), width_ * 4
  ) != 0;
#else
  LOGW("{}: stb_image_write not included.", __FUNCTION__);
  return false;
#endif
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"