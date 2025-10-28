#ifndef AER_SCENE_FONT_H_
#define AER_SCENE_FONT_H_

#include "aer/core/common.h"
#include "aer/core/utils.h"     // (for utils::FileReader)
#include "aer/scene/path_2d.h"

#include <stb/stb_truetype.h>

namespace scene {

/* -------------------------------------------------------------------------- */

class Font {
 public:
  static const std::u16string kDefaultCorpus;

  struct Glyph {
    Path2D path{};

    int32_t index{};
    int32_t advanceWidth{};
    int32_t leftSideBearing{};
  };

 public:
  Font() = default;
  ~Font() = default;

  [[nodiscard]]
  bool load(std::string_view filename);

  void generateGlyphs(
    std::u16string const& corpus = kDefaultCorpus,
    uint32_t curve_resolution = scene::Polyline::kDefaultCurveResolution,
    uint32_t line_resolution = scene::Path2D::kDefaultLineResolution
  );

  void release() {
    file_reader_.clear();
    glyph_map_.clear();
  }

  void writeAscii(std::u16string const& msg, int y_size = 18) const;

  [[nodiscard]]
  float pixelScaleFromSize(int fontsize) const noexcept;

  [[nodiscard]]
  bool hasGlyph(char16_t code) const {
    return glyph_map_.contains(code);
  }

  [[nodiscard]]
  Glyph & findGlyph(char16_t code) {
    return glyph_map_.at(code);
  }

  [[nodiscard]]
  Glyph const& findGlyph(char16_t code) const {
    return glyph_map_.at(code);
  }

  [[nodiscard]]
  int kern_advance(char16_t c1, char16_t c2) const;

  [[nodiscard]]
  auto const& glyph_map() const noexcept {
    return glyph_map_;
  }

 private:
  utils::FileReader file_reader_{};
  stbtt_fontinfo font_{};
  std::unordered_map<char16_t, Glyph> glyph_map_{};
  bool is_ttf_{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_FONT_H_
