#include "aer/scene/font_atlas.h"

#include <algorithm>
#include <bit>

#include "msdfgen.h"
#include "stb/stb_image.h"

#include "aer/core/utils.h"

// (only bake on desktop)
#if !defined(ANDROID)
#include <fstream>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_STATIC
#include "stb/stb_image_write.h"
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

// ----------------------------------------------------------------------------

constexpr char kBakedMagic[8]     = {'A', 'E', 'R', 'F', 'A', 'T', 'L', '1'};
constexpr uint32_t kBakedVersion  = 1u;
constexpr uint64_t kFnvOffset     = 0xcbf29ce484222325ull;

uint64_t Fnv1a(uint64_t hash, void const* data, size_t size) {
  auto const* bytes = static_cast<uint8_t const*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash = (hash ^ bytes[i]) * 0x100000001b3ull;
  }
  return hash;
}

template <typename T>
uint64_t Fnv1a(uint64_t hash, T const& value) {
  return Fnv1a(hash, &value, sizeof(T));
}

struct BakedHeader {
  char magic[8];
  uint32_t version;
  uint32_t width;
  uint32_t height;
  uint32_t glyph_count;
  uint32_t kern_count;
  uint32_t png_size;
  uint64_t key;
  uint64_t font_hash;
  float pixels_per_em;
  float px_range;
  uint32_t atlas_width;
  uint32_t _pad;
};

struct BakedGlyph {
  uint16_t code;
  uint8_t visible;
  uint8_t _pad;
  float advance;
  float plane[4];
  float uv[4];
};

struct BakedKern {
  uint16_t a;
  uint16_t b;
  float em;
};

#if !defined(NDEBUG)
static_assert(std::endian::native == std::endian::little, "little endian file format");
static_assert(sizeof(BakedHeader) == 64u);
static_assert(sizeof(BakedGlyph) == 40u);
static_assert(sizeof(BakedKern) == 8u);
#endif

} // namespace

/* -------------------------------------------------------------------------- */

uint64_t FontAtlas::MakeKey(std::u16string const& corpus, Params const& params) {
  uint64_t h = Fnv1a(kFnvOffset, kBakedVersion);
  h = Fnv1a(h, params.pixelsPerEm);
  h = Fnv1a(h, params.pxRange);
  h = Fnv1a(h, params.atlasWidth);
  return Fnv1a(h, corpus.data(), corpus.size() * sizeof(char16_t));
}

// ----------------------------------------------------------------------------

bool FontAtlas::LoadOrGenerate(
  FontAtlas& out,
  std::string_view const cache_file,
  [[maybe_unused]] std::string_view const font_file,
  std::u16string const& corpus,
  Params const& params
) {
  uint64_t const key = MakeKey(corpus, params);

#if !defined(ANDROID)
  std::vector<uint8_t> font_bytes;
  bool const has_font = utils::FileReader::Read(font_file, font_bytes);
  uint64_t const font_hash = has_font
    ? Fnv1a(kFnvOffset, font_bytes.data(), font_bytes.size())
    : 0ull;
#else
  bool const has_font = false;
  uint64_t const font_hash = 0ull;
#endif

  std::vector<uint8_t> baked;
  if (utils::FileReader::Read(cache_file, baked)) {
    uint64_t baked_font_hash = 0ull;
    if (out.load(baked, key, &baked_font_hash)
     && (!has_font || baked_font_hash == font_hash)) {
      LOGD("FontAtlas: loaded \"{}\".", cache_file);
      return true;
    }
    HLOGW("FontAtlas: \"{}\" is not valid.", cache_file);
  }

#if defined(ANDROID)
  HLOGW("FontAtlas: no baked atlas \"{}\".", cache_file);
  return false;
#else
  if (!has_font) {
    HLOGE("FontAtlas: neither \"{}\" nor \"{}\" can be read.", cache_file, font_file);
    return false;
  }

  Font font;
  if (!font.load(font_file)) {
    return false;
  }
  font.generateGlyphs(corpus, scene::Polyline::kDefaultCurveResolution);
  if (!out.generate(font, corpus, params)) {
    return false;
  }

  if (out.save(std::string(cache_file), key, font_hash)) {
    LOGI("FontAtlas: baked \"{}\".", cache_file);
  } else {
    HLOGW("FontAtlas: cannot write \"{}\".", cache_file);
  }
  return true;
#endif
}

// ----------------------------------------------------------------------------

bool FontAtlas::load(
  std::vector<uint8_t> const& bytes, uint64_t const key, uint64_t* font_hash
) {
  BakedHeader header{};
  if (bytes.size() < sizeof(header)) {
    return false;
  }
  std::memcpy(&header, bytes.data(), sizeof(header));
  if (std::memcmp(header.magic, kBakedMagic, sizeof(kBakedMagic)) != 0
   || header.version != kBakedVersion
   || header.key != key) {
    return false;
  }

  size_t const glyphs_size = static_cast<size_t>(header.glyph_count) * sizeof(BakedGlyph);
  size_t const kerns_size = static_cast<size_t>(header.kern_count) * sizeof(BakedKern);
  if (bytes.size() < sizeof(header) + glyphs_size + kerns_size + header.png_size) {
    return false;
  }
  uint8_t const* cursor = bytes.data() + sizeof(header);

  // The pixels first: nothing is modified if they are not valid.
  int w = 0, h = 0, channels = 0;
  uint8_t* decoded = stbi_load_from_memory(
    cursor + glyphs_size + kerns_size, static_cast<int>(header.png_size),
    &w, &h, &channels, 4
  );
  if (decoded == nullptr
   || static_cast<uint32_t>(w) != header.width
   || static_cast<uint32_t>(h) != header.height) {
    LOGW("FontAtlas: invalid baked pixels.");
    if (decoded) {
      stbi_image_free(decoded);
    }
    return false;
  }
  pixels_.assign(decoded, decoded + static_cast<size_t>(w) * h * 4u);
  stbi_image_free(decoded);

  params_ = { header.pixels_per_em, header.px_range, header.atlas_width };
  width_ = header.width;
  height_ = header.height;

  glyphs_.clear();
  for (uint32_t i = 0; i < header.glyph_count; ++i) {
    BakedGlyph g{};
    std::memcpy(&g, cursor + i * sizeof(g), sizeof(g));
    glyphs_[g.code] = GlyphInfo{
      .code = static_cast<char16_t>(g.code),
      .visible = g.visible != 0u,
      .advance = g.advance,
      .planeL = g.plane[0],
      .planeB = g.plane[1],
      .planeR = g.plane[2],
      .planeT = g.plane[3],
      .texcoord = { g.uv[0], g.uv[1], g.uv[2], g.uv[3] },
    };
  }
  cursor += glyphs_size;

  kerning_.clear();
  for (uint32_t i = 0; i < header.kern_count; ++i) {
    BakedKern k{};
    std::memcpy(&k, cursor + i * sizeof(k), sizeof(k));
    kerning_[(static_cast<uint32_t>(k.a) << 16) | k.b] = k.em;
  }

  if (font_hash) {
    *font_hash = header.font_hash;
  }
  return true;
}

// ----------------------------------------------------------------------------

#if !defined(ANDROID)

bool FontAtlas::save(
  std::string const& filename, uint64_t const key, uint64_t const font_hash
) const {
  if (pixels_.empty()) {
    return false;
  }

  int png_size = 0;
  uint8_t* png = stbi_write_png_to_mem(
    pixels_.data(), static_cast<int>(width_ * 4u),
    static_cast<int>(width_), static_cast<int>(height_), 4, &png_size
  );
  if (png == nullptr) {
    return false;
  }

  // Sorted: the same atlas gives the same file.
  std::vector<BakedGlyph> glyphs;
  glyphs.reserve(glyphs_.size());
  for (auto const& [code, g] : glyphs_) {
    glyphs.push_back({
      .code = static_cast<uint16_t>(code),
      .visible = g.visible ? uint8_t{1} : uint8_t{0},
      .advance = g.advance,
      .plane = { g.planeL, g.planeB, g.planeR, g.planeT },
      .uv = { g.texcoord.x, g.texcoord.y, g.texcoord.z, g.texcoord.w },
    });
  }
  std::sort(glyphs.begin(), glyphs.end(), [](auto const& a, auto const& b) {
    return a.code < b.code;
  });

  std::vector<BakedKern> kerns;
  kerns.reserve(kerning_.size());
  for (auto const& [pair, em] : kerning_) {
    kerns.push_back({
      static_cast<uint16_t>(pair >> 16), static_cast<uint16_t>(pair & 0xffffu), em
    });
  }
  std::sort(kerns.begin(), kerns.end(), [](auto const& a, auto const& b) {
    return (a.a != b.a) ? (a.a < b.a) : (a.b < b.b);
  });

  BakedHeader header{};
  std::memcpy(header.magic, kBakedMagic, sizeof(kBakedMagic));
  header.version = kBakedVersion;
  header.width = width_;
  header.height = height_;
  header.glyph_count = static_cast<uint32_t>(glyphs.size());
  header.kern_count = static_cast<uint32_t>(kerns.size());
  header.png_size = static_cast<uint32_t>(png_size);
  header.key = key;
  header.font_hash = font_hash;
  header.pixels_per_em = params_.pixelsPerEm;
  header.px_range = params_.pxRange;
  header.atlas_width = params_.atlasWidth;

  std::ofstream file(filename, std::ios::binary | std::ios::trunc);
  bool ok = static_cast<bool>(file);
  if (ok) {
    file.write(reinterpret_cast<char const*>(&header), sizeof(header));
    file.write(
      reinterpret_cast<char const*>(glyphs.data()),
      static_cast<std::streamsize>(glyphs.size() * sizeof(BakedGlyph))
    );
    file.write(
      reinterpret_cast<char const*>(kerns.data()),
      static_cast<std::streamsize>(kerns.size() * sizeof(BakedKern))
    );
    file.write(reinterpret_cast<char const*>(png), png_size);
    ok = static_cast<bool>(file);
  }
  STBIW_FREE(png);

  return ok;
}

#else // defined(ANDROID)

bool FontAtlas::save(std::string const&, uint64_t, uint64_t) const {
  LOGW("FontAtlas::save is not available on Android.");
  return false;
}

#endif

// ----------------------------------------------------------------------------

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

  kerning_.clear();
  for (auto const& [a, ga] : glyphs_) {
    for (auto const& [b, gb] : glyphs_) {
      int const kern = font.getGlyphKernAdvance(a, b);
      if (kern != 0) {
        auto kern_key = (static_cast<uint32_t>(a) << 16)
                      | static_cast<uint32_t>(b)
                      ;
        kerning_[kern_key] = static_cast<float>(kern * em_scale);
      }
    }
  }

  LOGD("FontAtlas: {} glyphs, {}x{}, {} kerning",
    cells.size(), width_, height_, kerning_.empty()?"without":"with");

  return true;
}

// ----------------------------------------------------------------------------

FontAtlas::TextLayout FontAtlas::layout(
  Font const& font,
  std::u16string const& text,
  bool kerning
) const {
  LOG_CHECK(font.loaded());
  float const em_scale = static_cast<float>(font.emScale());

  TextLayout out{};
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
    // --------------------------------------
    if (kerning && (i + 1 < text.size())) {
      pen += font.getGlyphKernAdvance(text[i], text[i + 1]) * em_scale;
    }
    // --------------------------------------
  }
  out.width = pen;
  return out;
}

// ----------------------------------------------------------------------------

FontAtlas::TextLayout FontAtlas::layout(
  std::u16string const& text,
  bool kerning
) const {
  TextLayout out{};
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
    if (kerning && ((i + 1) < text.size())) {
      // --------------------------------------
      auto const it = kerning_.find(
        (static_cast<uint32_t>(text[i]) << 16) | static_cast<uint32_t>(text[i + 1])
      );
      if (it != kerning_.end()) {
        pen += it->second;
      }
      // --------------------------------------
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

  bool result = false;

#if defined(INCLUDE_STB_IMAGE_WRITE_H)
  result = stbi_write_png(
    filename.c_str(), width_, height_, 4, pixels_.data(), width_ * 4
  ) != 0;
#else
  LOGW("{}: stb_image_write not included.", __FUNCTION__);
#endif

  return result;
}

/* -------------------------------------------------------------------------- */

} // namespace "scene"