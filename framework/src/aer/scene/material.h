#ifndef AER_SCENE_MATERIAL_H_
#define AER_SCENE_MATERIAL_H_

#include "aer/core/common.h"

namespace scene {

/* -------------------------------------------------------------------------- */

/* Static pipeline states of a material. */
struct MaterialStates {
  bool operator==(MaterialStates const& other) const noexcept {
    return alpha_mode == other.alpha_mode;
  }

  bool operator<(MaterialStates const& other) const noexcept {
    if (*this != other) {
      return alpha_mode < other.alpha_mode;
    }
    return false;
  }

  struct Hasher {
    size_t operator()(MaterialStates const& s) const noexcept {
      return std::hash<int>()(static_cast<int>(s.alpha_mode));
    }
  };

  enum class AlphaMode {
    Opaque,
    Mask,
    Blend,
    kCount
  } alpha_mode{AlphaMode::Opaque};
};

// ----------------------------------------------------------------------------

enum class MaterialModel {
  Unknown,
  PBRMetallicRoughness,
  Unlit,
  kCount,
};

// ----------------------------------------------------------------------------

/* Lazy host-side material proxy to be translated by the renderer. */
struct MaterialProxy {
  struct TextureBinding {
    uint32_t basecolor{kInvalidIndexU32};
    uint32_t normal{kInvalidIndexU32};
    uint32_t occlusion{kInvalidIndexU32};
    uint32_t emissive{kInvalidIndexU32};
    uint32_t roughness_metallic{kInvalidIndexU32};
  } bindings{};

  vec3 emissive_factor{};
  float alpha_cutoff{0.5f};
  bool double_sided{};

  struct {
    vec4 basecolor_factor{};
    float metallic_factor{};
    float roughness_factor{};
  } pbr_mr;
};

// ----------------------------------------------------------------------------

/* Link a material to its MaterialFx. */
struct MaterialRef {
  // Built-in model id for the material.
  MaterialModel model{}; //

  // Static pipeline states.
  MaterialStates states{};

  // Index to the global resources proxy material.
  uint32_t proxy_index{}; //

  // Index of the material in the MaterialFx internal buffer.
  uint32_t material_index{ kInvalidIndexU32 };
};

/* -------------------------------------------------------------------------- */

}  // namespace scene

#endif // AER_SCENE_MATERIAL_H_
