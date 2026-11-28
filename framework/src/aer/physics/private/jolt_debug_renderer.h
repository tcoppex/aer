#ifndef AER_PHYSICS_PRIVATE_JOLT_DEBUG_RENDERER_H_
#define AER_PHYSICS_PRIVATE_JOLT_DEBUG_RENDERER_H_

#include "aer/physics/private/jolt.h"

/* -------------------------------------------------------------------------- */

#ifdef JPH_DEBUG_RENDERER

#include "aer/core/common.h"
#include <Jolt/Renderer/DebugRendererSimple.h>

namespace physics {

class DebugLineRenderer final : public JPH::DebugRendererSimple {
 public:
  void begin(DebugLineFn const* sink) noexcept {
    sink_ = sink;
  }

  void end() noexcept {
    sink_ = nullptr;
  }

  void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override {
    if (sink_) {
      (*sink_)(FromJolt(from), FromJolt(to), ToLinear(color));
    }
  }

  void DrawText3D(JPH::RVec3Arg, std::string_view const&, JPH::ColorArg, float) override {}

 private:
  /* Jolt colors are sRGB bytes, DebugDraw expects linear RGB. */
  static vec3 ToLinear(JPH::ColorArg c) {
    auto const to_linear = [](JPH::uint8 v) {
      return lina::to_linear_channel(static_cast<float>(v) / 255.0f);
    };
    return vec3(to_linear(c.r), to_linear(c.g), to_linear(c.b));
  }

 private:
  DebugLineFn const* sink_{nullptr};
};

} // namespace "physics"

#endif // JPH_DEBUG_RENDERER

/* -------------------------------------------------------------------------- */

#endif // AER_PHYSICS_PRIVATE_JOLT_DEBUG_RENDERER_H_
