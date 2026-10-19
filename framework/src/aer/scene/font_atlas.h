#ifndef AER_SCENE_FONT_ATLAS_H_
#define AER_SCENE_FONT_ATLAS_H_

#include "aer/core/common.h"
#include "aer/scene/font.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace scene {

/* -------------------------------------------------------------------------- */

/*
 * Conventions (Y-up, "em" units):
 *  - Pixels are RGBA8, *linear* (NOT sRGB).
 *  - Atlas row 0 is the TOP of the image (rows are flipped from msdfgen,
 *    whose row 0 is the bottom of the glyph).
 *  - Per glyph, the quad spans plane bounds [planeL,planeR]x[planeB,planeT]
 *    relative to the pen position and samples uv [uL,uR]x[vTop,vBottom].
 */
class FontAtlas {
 public:
  using GlyphTexcoord = vec4;

  struct GlyphInfo {
    char16_t code{};
    bool visible{};      // false for glyphs without outline (e.g. space)
    float advance{};     // em

    // Quad, relative to pen position (em).
    float planeL{};
    float planeB{};
    float planeR{};
    float planeT{};

    GlyphTexcoord texcoord{};
  };

  struct Quad {
    vec2 position{};  // bottom-left corner (em)
    vec2 size{};
    GlyphTexcoord texcoord{};
  };

  struct TextLayout {
    std::vector<Quad> quads{};
    float width{};  // total advance (em)
  };

  struct Params {
    float pixelsPerEm{48.0f};  // glyph resolution inside the atlas
    float pxRange{4.0f};       // distance range in atlas pixels
    uint32_t atlasWidth{512u};
  };

 public:
  [[nodiscard]]
  static uint64_t MakeKey(std::u16string const& corpus, Params const& params);

  /* Try to load a cache .mtsdf file or generating it if none were found. */
  [[nodiscard]]
  static bool LoadOrGenerate(
    FontAtlas& out,
    std::string_view cache_file,
    std::string_view font_file,
    std::u16string const& corpus,
    Params const& params
  );

 public:
  [[nodiscard]]
  bool load(std::vector<uint8_t> const& bytes, uint64_t key, uint64_t* font_hash = nullptr);

  [[nodiscard]]
  bool save(std::string const& filename, uint64_t key, uint64_t font_hash) const;

  [[nodiscard]]
  bool generate(Font const& font, std::u16string const& corpus, FontAtlas::Params const& params);

  [[nodiscard]]
  bool generate(Font const& font, std::u16string const& corpus = Font::kDefaultCorpus) {
    return generate(font, corpus, {});
  }

  [[nodiscard]]
  TextLayout layout(Font const& font, std::u16string const& text, bool kerning = true) const;

  [[nodiscard]]
  TextLayout layout(std::u16string const& text, bool kerning = true) const;

  bool writePNG(std::string const& filename) const;

  [[nodiscard]]
  uint32_t width() const noexcept {
    return width_;
  }

  [[nodiscard]]
  uint32_t height() const noexcept {
    return height_;
  }

  [[nodiscard]]
  float pxRange() const noexcept {
    return params_.pxRange;
  }

  [[nodiscard]]
  auto const& pixels() const noexcept {
    return pixels_;
  }

  [[nodiscard]]
  auto const& glyphs() const noexcept {
    return glyphs_;
  }

  [[nodiscard]]
  GlyphInfo const* findGlyph(char16_t c) const {
    auto const it = glyphs_.find(c);
    return (it == glyphs_.end()) ? nullptr : &it->second;
  }

 private:
  Params params_{};
  uint32_t width_{};
  uint32_t height_{};
  std::vector<uint8_t> pixels_{};  // RGBA8, width_ * height_ * 4
  std::unordered_map<char16_t, GlyphInfo> glyphs_{};
  std::unordered_map<uint32_t, float> kerning_{};  // (c1 << 16 | c2) -> em
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_FONT_ATLAS_H_
