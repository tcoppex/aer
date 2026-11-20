#ifndef AER_SCENE_PRIVATE_EXTRACT_ANIMATION_HELPERS_H_
#define AER_SCENE_PRIVATE_EXTRACT_ANIMATION_HELPERS_H_

#include <vector>

#include "aer/scene/animation.h"
#include "aer/scene/load_options.h"
#include "aer/scene/private/cgltf_wrapper.h"

/* -------------------------------------------------------------------------- */

//
// A glTF animation channel with its sampler, unpacked to host floats.
//
struct ChannelData {
  cgltf_animation_path_type path{};
  cgltf_interpolation_type interpolation{};
  uint32_t components{};        // 3 for translation / scale, 4 for rotation
  std::vector<float> times{};
  std::vector<float> values{};  // `components` floats per key, ×3 for cubic splines

  [[nodiscard]]
  bool is_cubic() const noexcept {
    return interpolation == cgltf_interpolation_type_cubic_spline;
  }

  [[nodiscard]]
  size_t stride() const noexcept {
    return components * (is_cubic() ? 3u : 1u);
  }
};

struct JointChannel {
  uint32_t joint{};
  ChannelData data{};
};

// Returns false when the channel is invalid or targets something unsupported.
bool LoadChannelData(cgltf_animation_channel const& channel, ChannelData& out);

// Value of the channel at `time`.
vec4 EvaluateChannel(ChannelData const& channel, float time);

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_PRIVATE_EXTRACT_ANIMATION_HELPERS_H_
