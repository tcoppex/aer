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
  static const std::u16string kDefaultChars;

  struct Glyph {
    int32_t index{};
    Path2D path{};
  };

 public:
  Font() = default;
  ~Font() = default;

  [[nodiscard]]
  bool load(std::string_view filename);

  void generate(
    std::u16string const& corpus = kDefaultChars,
    float const curve_resolution = scene::Polyline::kDefaultCurveResolution
  );

  void release() {
    file_reader_.clear();
    glyph_map_.clear();
  }

  void writeAscii(std::u16string const& msg, int y_size = 18) const;

  [[nodiscard]]
  float pixelScaleFromSize(int fontsize) const noexcept;

  [[nodiscard]]
  Glyph & findGlyph(uint16_t code) {
    return glyph_map_.at(code);
  }

  [[nodiscard]]
  Glyph const& findGlyph(uint16_t code) const {
    return findGlyph(code);
  }

 private:
  utils::FileReader file_reader_{};
  stbtt_fontinfo font_{};
  std::unordered_map<uint16_t, Glyph> glyph_map_{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_FONT_H_
