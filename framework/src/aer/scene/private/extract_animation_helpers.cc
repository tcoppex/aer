#include "aer/scene/private/extract_animation_helpers.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

/* -------------------------------------------------------------------------- */

namespace {

vec4 ReadVec(float const* v, uint32_t components) {
  return vec4(v[0], v[1], v[2], (components == 4u) ? v[3] : 0.0f);
}

} // namespace ""

// ----------------------------------------------------------------------------

bool LoadChannelData(cgltf_animation_channel const& channel, ChannelData& out) {
  cgltf_animation_sampler const* sampler = channel.sampler;
  if (!sampler || !sampler->input || !sampler->output) {
    return false;
  }

  switch (channel.target_path) {
    case cgltf_animation_path_type_translation:
    case cgltf_animation_path_type_scale:
      out.components = 3u;
    break;

    case cgltf_animation_path_type_rotation:
      out.components = 4u;
    break;

    default:
      return false;
  }
  out.path = channel.target_path;
  out.interpolation = sampler->interpolation;

  auto unpack = [](cgltf_accessor const* accessor, std::vector<float>& dst) {
    dst.resize(cgltf_accessor_unpack_floats(accessor, nullptr, 0));
    return cgltf_accessor_unpack_floats(accessor, dst.data(), dst.size()) == dst.size();
  };
  if (!unpack(sampler->input, out.times) || !unpack(sampler->output, out.values)) {
    return false;
  }

  size_t const key_count = out.times.size();
  if (key_count == 0u || out.values.size() != key_count * out.stride()) {
    return false;
  }
  if (out.is_cubic() && key_count < 2u) {
    return false;
  }

  // Keys of a rotation are expected to be unit quaternions.
  if (out.path == cgltf_animation_path_type_rotation && !out.is_cubic()) {
    for (size_t k = 0; k < key_count; ++k) {
      float* v = out.values.data() + k * 4u;
      quat q(v);
      float const len = lina::length(q);
      if (len > lina::kEpsilon) {
        q = q / len;
        std::copy(lina::ptr(q), lina::ptr(q) + 4, v);
      }
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

vec4 EvaluateChannel(ChannelData const& channel, float time) {
  size_t const key_count = channel.times.size();
  size_t const stride = channel.stride();
  uint32_t const comp = channel.components;
  float const* values = channel.values.data();

  // Value of the key k (skipping the tangents of cubic splines).
  auto key_value = [&](size_t k) {
    return ReadVec(values + k * stride + (channel.is_cubic() ? comp : 0u), comp);
  };

  if (key_count == 1u || time <= channel.times.front()) {
    return key_value(0u);
  }
  if (time >= channel.times.back()) {
    return key_value(key_count - 1u);
  }

  // times[k] <= time < times[k+1]
  size_t const k = static_cast<size_t>(
    std::upper_bound(channel.times.begin(), channel.times.end(), time) - channel.times.begin()
  ) - 1u;
  float const dt = channel.times[k + 1u] - channel.times[k];
  float const alpha = std::clamp((time - channel.times[k]) / dt, 0.0f, 1.0f);

  switch (channel.interpolation) {
    case cgltf_interpolation_type_step:
      return key_value(k);

    case cgltf_interpolation_type_cubic_spline: {
      float const* v0 = values + k * stride;
      float const* v1 = values + (k + 1u) * stride;

      vec4 const p0 = ReadVec(v0 + comp,      comp);
      vec4 const m0 = ReadVec(v0 + 2u * comp, comp) * dt;  // out tangent of k
      vec4 const p1 = ReadVec(v1 + comp,      comp);
      vec4 const m1 = ReadVec(v1,             comp) * dt;  // in tangent of k+1

      float const a2 = alpha * alpha;
      float const a3 = a2 * alpha;
      vec4 result = p0 * (2.0f * a3 - 3.0f * a2 + 1.0f)
                  + m0 * (a3 - 2.0f * a2 + alpha)
                  + p1 * (-2.0f * a3 + 3.0f * a2)
                  + m1 * (a3 - a2);
      if (comp == 4u) {
        result = lina::normalize(result);
      }
      return result;
    }

    default: // linear
      if (comp == 4u) {
        return lina::qlerp(key_value(k), key_value(k + 1u), alpha);
      }
      return lina::lerp(key_value(k), key_value(k + 1u), alpha);
  }
}

/* -------------------------------------------------------------------------- */
