#include "aer/renderer/debug_draw.h"

#include <algorithm>

#include "aer/scene/camera.h"

/* -------------------------------------------------------------------------- */

namespace {

uint32_t PackColor(vec3 const& c) {
  auto const to_byte = [](float v) {
    return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  return to_byte(c.x) | (to_byte(c.y) << 8u) | (to_byte(c.z) << 16u) | (255u << 24u);
}

}  // namespace

/* -------------------------------------------------------------------------- */

bool DebugDraw::init(RenderContext& context, uint32_t frames_in_flight) {
  context_ptr_ = &context;

  pipeline_layout_ = context.createPipelineLayout({
    .pushConstantRanges = {
      {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(PushConstant),
      }
    },
  });
  if (pipeline_layout_ == VK_NULL_HANDLE) {
    return false;
  }

  auto const shader = context.createShaderModule(
    FRAMEWORK_COMPILED_SHADERS_DIR "debug/", "debug_draw.slang"
  );

  auto const create_pipeline = [&](bool depth_test) {
    return context.createGraphicsPipeline(pipeline_layout_, {
      .vertex = {
        .module = shader.handle,
        .entryPoint = "vertexMain",
        .buffers = {
          {
            .stride = sizeof(Segment),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
            .attributes = {
              { 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Segment, a) },
              { 1u, 0u, VK_FORMAT_R32_SFLOAT,       offsetof(Segment, width) },
              { 2u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Segment, b) },
              { 3u, 0u, VK_FORMAT_R8G8B8A8_UNORM,   offsetof(Segment, color_a) },
              { 4u, 0u, VK_FORMAT_R8G8B8A8_UNORM,   offsetof(Segment, color_b) },
            },
          }
        },
      },
      .fragment = {
        .module = shader.handle,
        .entryPoint = "fragmentMain",
        .targets = {
          { .writeMask = GraphicsPipelineDescriptor_t::kDefaultColorWriteMask }
        },
      },
      .depthStencil = {
        .depthTestEnable = depth_test ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
      },
      .primitive = {
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      },
    });
  };

  layers_[static_cast<size_t>(Depth::Off)].pipeline = create_pipeline(false);
  layers_[static_cast<size_t>(Depth::On)].pipeline  = create_pipeline(true);
  context.releaseShaderModule(shader);

  for (auto& layer : layers_) {
    layer.segments.reserve(1024u);
    layer.buffers.resize(frames_in_flight);
    for (auto& buffer : layer.buffers) {
      buffer = context.createBuffer(
        "DebugDraw::Segments",
        kMaxSegments * sizeof(Segment),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT
      );
      if (!buffer.valid()) {
        return false;
      }
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

void DebugDraw::release() {
  if (!context_ptr_) {
    return;
  }
  for (auto& layer : layers_) {
    for (auto const& buffer : layer.buffers) {
      context_ptr_->destroyBuffer(buffer);
    }
    layer.buffers.clear();
    context_ptr_->destroyResources(layer.pipeline);
  }
  context_ptr_->destroyResources(pipeline_layout_);
  context_ptr_ = nullptr;
}

// ----------------------------------------------------------------------------

void DebugDraw::update(Camera const& camera) {
  view_projection_ = camera.transforms()[0].view_projection;
}

// ----------------------------------------------------------------------------

void DebugDraw::render(RenderPassEncoder const& pass, uint32_t frame_slot, vec2 const& viewport) {
  if (dropped_ > 0u) {
    LOGW("[DebugDraw] {} segment(s) dropped (capacity {}).", dropped_, kMaxSegments);
    dropped_ = 0u;
  }

  PushConstant const pc{
    .view_projection = view_projection_,
    .viewport = viewport,
  };

  for (auto& layer : layers_) {
    if (layer.segments.empty()) {
      continue;
    }
    if (!context_ptr_ || frame_slot >= layer.buffers.size()) {
      layer.segments.clear();
      continue;
    }

    auto const& buffer = layer.buffers[frame_slot];
    context_ptr_->writeBuffer(
      buffer, 0u,
      layer.segments.data(), 0u,
      layer.segments.size() * sizeof(Segment)
    );

    pass.bindPipeline(layer.pipeline);
    pass.pushConstant(pc, VK_SHADER_STAGE_VERTEX_BIT);
    pass.bindVertexBuffer(buffer);
    pass.draw(4u, static_cast<uint32_t>(layer.segments.size()));

    layer.segments.clear();
  }
}

// ----------------------------------------------------------------------------

void DebugDraw::cross(
  vec3 const& p,
  float half_extent,
  vec3 const& color,
  float width, Depth depth
) {
  vec3 const x(half_extent, 0.0f, 0.0f);
  vec3 const y(0.0f, half_extent, 0.0f);
  vec3 const z(0.0f, 0.0f, half_extent);
  append(p - x, p + x, color, color, width, depth);
  append(p - y, p + y, color, color, width, depth);
  append(p - z, p + z, color, color, width, depth);
}

// ----------------------------------------------------------------------------

void DebugDraw::append(
  vec3 const& a,
  vec3 const& b,
  vec3 const& color_a,
  vec3 const& color_b,
  float width,
  Depth depth
) {
  auto& segments = layers_[static_cast<size_t>(depth)].segments;
  if (segments.size() >= kMaxSegments) {
    ++dropped_;
    return;
  }
  segments.push_back({
    .a       = a,
    .width   = std::max(width, 0.0f),
    .b       = b,
    .color_a = PackColor(color_a),
    .color_b = PackColor(color_b),
  });
}

/* -------------------------------------------------------------------------- */
