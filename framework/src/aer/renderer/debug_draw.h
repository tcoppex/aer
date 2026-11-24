#ifndef AER_RENDERER_DEBUG_DRAW_H_
#define AER_RENDERER_DEBUG_DRAW_H_

#include <array>
#include <vector>

#include "aer/core/common.h"
#include "aer/platform/vulkan/command_encoder.h"
#include "aer/renderer/render_context.h"
#include "aer/renderer/pipeline.h"

class Camera;

/* -------------------------------------------------------------------------- */

//
//    DebugDraw
//
//  Immediate mode lines and points drawn over the scene.
//
//  - Submit from Application::update / lateUpdate (before the frame is drawn),
//  everything is rendered once then cleared.
//  - Thickness is in pixels.
//  - Colors are linear RGB.
//  - Only the first view of the camera is used (no XR).
//
class DebugDraw {
 public:
  static constexpr uint32_t kMaxSegments = 1u << 15u;

  enum class Depth : uint8_t {
    Off,
    On,
  };

 public:
  bool init(RenderContext& context, uint32_t frames_in_flight);

  void release();

  /* Camera of the frame. */
  void update(Camera const& camera);

  /* Draw and clear the segments submitted since the last call. */
  void render(RenderPassEncoder const& pass, uint32_t frame_slot, vec2 const& viewport);

  // --- Submission ---

  void line(
    vec3 const& a,
    vec3 const& b,
    vec3 const& color,
    float width = 1.0f,
    Depth depth = Depth::Off
  ) {
    append(a, b, color, color, width, depth);
  }

  void line(
    vec3 const& a,
    vec3 const& color_a,
    vec3 const& b,
    vec3 const& color_b,
    float width = 1.0f,
    Depth depth = Depth::Off
  ) {
    append(a, b, color_a, color_b, width, depth);
  }

  /* Square point, `size` in pixels. */
  void point(
    vec3 const& p,
    vec3 const& color,
    float size = 4.0f,
    Depth depth = Depth::Off
  ) {
    append(p, p, color, color, size, depth);
  }

  /* Three axis aligned lines, `half_extent` in world units, `width` in pixels. */
  void cross(
    vec3 const& p,
    float half_extent,
    vec3 const& color,
    float width = 1.0f,
    Depth depth = Depth::Off
  );

 private:
  struct Segment {
    vec3 a;
    float width;
    vec3 b;
    uint32_t color_a;   // RGBA8
    uint32_t color_b;
  };

  struct PushConstant {
    mat4 view_projection;
    vec2 viewport;
    vec2 padding{};
  };

  struct Layer {
    std::vector<Segment> segments{};
    std::vector<backend::Buffer> buffers{};   // one per frame slot
    Pipeline pipeline{};
  };

 private:
  void append(
    vec3 const& a, vec3 const& b,
    vec3 const& color_a, vec3 const& color_b,
    float width, Depth depth
  );

 private:
  RenderContext* context_ptr_{};

  mat4 view_projection_{lina::identity};
  VkPipelineLayout pipeline_layout_{};
  std::array<Layer, 2u> layers_{};   // EnumArray<Depth>
  uint32_t dropped_{};
};

/* -------------------------------------------------------------------------- */

#endif // AER_RENDERER_DEBUG_DRAW_H_
