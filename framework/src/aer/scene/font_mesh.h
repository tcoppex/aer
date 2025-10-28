#ifndef AER_SCENE_FONT_MESH_H_
#define AER_SCENE_FONT_MESH_H_

#include "aer/core/common.h"
#include "aer/scene/mesh.h"
#include "aer/scene/font.h"

namespace scene {

/* -------------------------------------------------------------------------- */

class FontMesh : public Mesh {
 public:
  struct GlyphInfo {
    uint32_t primitive_index{};
    uint32_t count{};
  };

  struct GlyphDrawInfo {
    char16_t code{};
    mat4 matrix{linalg::identity};
    std::span<const SubMesh> submeshes{};
  };

  struct TextDrawInfo {
    std::vector<GlyphDrawInfo> glyphs{};
    int32_t cx{};
  };

 public:
  FontMesh() = default;
  ~FontMesh() = default;

  void reset() {
    *this = {};
  }

  [[nodiscard]]
  bool generate(
    Font const& font,
    float extrusionDepth = Path2D::kDefaultExtrusionDepth,
    uint32_t extrusionSampleCount = Path2D::kDefaultExtrusionSampleCount
  );

  [[nodiscard]]
  TextDrawInfo buildTextDrawInfo(
    std::u16string const& text,
    bool enableKerning = true
  ) const;

  [[nodiscard]]
  auto glyph_submeshes(char16_t code) const {
    if (auto it = glyph_info_map_.find(code); it == glyph_info_map_.end()) {
      return std::span(submeshes).subspan(0,0);
    }
    auto glyph_info = glyph_info_map_.at(code);
    return std::span(submeshes).subspan(
      glyph_info.primitive_index,
      glyph_info.count
    );
  }

 private:
  Font const* font_ptr_{};
  std::unordered_map<char16_t, GlyphInfo> glyph_info_map_{};
};

/* -------------------------------------------------------------------------- */

} // namespace "scene"

#endif // AER_SCENE_FONT_MESH_H_