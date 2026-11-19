/* -------------------------------------------------------------------------- */
//
//    11 - ray tracing
//
//  Where we illuminates the scene one ray at a time.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "aer/renderer/fx/postprocess/ray_tracing/ray_tracing_fx.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class BasicRayTracingFx : public RayTracingFx {
 public:
  BasicRayTracingFx() = default;

  void resetFrameAccumulation() final {
    push_constant_.accumulation_frame_count = 0;
  }

  void setupUI() final {
    bool changed = false;

    changed |= ImGui::Checkbox("Enable", &enabled_);

    ImGui::SliderInt(
      "Max Accumulation Frame",
      &max_accumulation_frame_count_,
      1, 256
    );

    changed |= ImGui::SliderInt(
      "Samples",
      &push_constant_.num_samples,
      1, 64
    );

    changed |= ImGui::SliderFloat(
      "Jitter factor",
      &push_constant_.jitter_factor,
      1.0f, 100.0f, "%.1f"
    );

    changed |= ImGui::SliderFloat(
      "Emissive strength",
      &push_constant_.light_intensity,
      0.0f, 64.0f, "%.1f"
    );

    changed |= ImGui::SliderFloat(
      "Sky intensity",
      &push_constant_.sky_intensity,
      0.0f, 5.0f, "%.1f"
    );

    ImGui::Text("Accumulation frame count: %d", push_constant_.accumulation_frame_count);

    if (changed) {
      resetFrameAccumulation();
    }
  }

  void execute(CommandEncoder const& cmd) const final {
    if (push_constant_.accumulation_frame_count < max_accumulation_frame_count_) {
      RayTracingFx::execute(cmd);   // (does nothing if the view is not valid)
    }
  }

  void set_view(RayTracingView const& view) final {
    RayTracingFx::set_view(view);
    push_constant_.frame_buffer_address    = view.frame_data_address;
    push_constant_.geometry_table_address  = view.geometry_table_address;

    if (view.generation != last_generation_) {
      last_generation_ = view.generation;
      resetFrameAccumulation();
    }
  }

 protected:
  backend::ShadersMap createShaderModules() const final {
    auto make_modules{[&](backend::ShaderStage stage, std::vector<std::string_view> const& filenames) {
      return backend::ShadersMap::value_type{
        stage,
        context_ptr_->createShaderModules(SAMPLE_SPIRV_DIR, filenames)
      };
    }};

    return {
      make_modules( backend::ShaderStage::Raygen,     { "raygen.slang" }),
      make_modules( backend::ShaderStage::ClosestHit, { "closesthit.slang" }),
      make_modules( backend::ShaderStage::AnyHit,     { "anyhit.slang" }),
      make_modules( backend::ShaderStage::Miss,       { "miss.slang" }),
    };
  }

  RayTracingPipelineDescriptor_t pipelineDescriptor(
    backend::ShadersMap const& shaders_map
  ) final {
    auto shader_index{[&](std::string_view shader_name) -> uint32_t {
      uint32_t index = 0;
      for (auto const& [stage, shaders] : shaders_map) {
        for (auto const& shader : shaders) {
          if (shader.basename == shader_name) {
            return index;
          }
          ++index;
        }
      }
      return kInvalidIndexU32;
    }};

    return {
      .shaders = {
        .raygens      = shaders_map.at(backend::ShaderStage::Raygen),
        .anyHits      = shaders_map.at(backend::ShaderStage::AnyHit),
        .closestHits  = shaders_map.at(backend::ShaderStage::ClosestHit),
        .misses       = shaders_map.at(backend::ShaderStage::Miss),
      },

      .shaderGroups = {
        // Raygen Groups
        .raygens = {{
          .type          = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
          .generalShader = shader_index("raygen.slang"),
        }},

        // Miss Groups
        .misses = {{
          .type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
          .generalShader  = shader_index("miss.slang"),
        }},

        // Hit Groups
        .hits = {{
          .type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
          .closestHitShader   = shader_index("closesthit.slang"),
          .anyHitShader       = shader_index("anyhit.slang"),
          .intersectionShader = VK_SHADER_UNUSED_KHR, // only on PROCEDURAL type
        }},
      }
    };
  }

  std::vector<VkPushConstantRange> push_constant_ranges() const final {
    return {
      {
        .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
                    | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
                    | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                    | VK_SHADER_STAGE_MISS_BIT_KHR
                    ,
        .size = sizeof(push_constant_),
      }
    };
  }

  void pushConstant(GenericCommandEncoder const &cmd) const final {
    cmd.pushConstant(
      push_constant_,
      pipeline_layout_,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR
      | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
      | VK_SHADER_STAGE_MISS_BIT_KHR
      | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
    );
    push_constant_.accumulation_frame_count += 1u;
  }

 private:
  int32_t max_accumulation_frame_count_{100};

  uint64_t last_generation_{~uint64_t(0)};

  mutable shader_interop::PushConstant push_constant_{
    .num_samples      = 8,
    .jitter_factor    = 2.0f,
    .light_intensity  = 40.0f,
    .sky_intensity    = 0.6f,
  };

};

/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */
/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  bool setup() final {

    auto const& features = context_.features();
    if (!features.ray_tracing_pipeline.rayTracingPipeline) {
      LOGW("This device does not support ray tracing pipeline.");
      return false;
    }
    if (!renderer_.setRayTracingEnabled(true)) {
      return false;
    }

    wm_->set_title("11 - shining through");

    renderer_.set_clear_color({ 0.16f, 0.14f, 0.12f, 1.0f });

    /* Setup the ArcBall camera. */
    {
      camera_.makePerspective(
        lina::radians(55.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.1f,
        100.0f
      );
      camera_.set_controller(&arcball_controller_);

      arcball_controller_.set_target(vec3f(0.0f, 1.0f, 0.0));
      arcball_controller_.set_view(0.0f, 0.0f);
      arcball_controller_.set_dolly(3.5f);
    }

    /* Setup the RayTracing effect. */
    ray_tracing_fx_.init(context_);
    ray_tracing_fx_.setup(renderer_.surface_size());

    /* Load a glTF Scene. */
    root_ = renderer_.load(ASSETS_DIR "models/CornellBox-Original.gltf");
    if (root_ == entt::null) {
      return false;
    }

    return true;
  }

  void release() final {
    ray_tracing_fx_.release();
    if (root_ != entt::null) {
      renderer_.destroyInstance(root_);
      root_ = entt::null;
    }
  }

  void update(float const dt) final {
    renderer_.set_rasterization_enabled(!ray_tracing_fx_.is_enable());

    if (camera_.rebuilt()) {
      ray_tracing_fx_.resetFrameAccumulation();
    }
  }

  void draw(CommandEncoder const& cmd) final {
    bool traced = false;

    // -- RAY TRACING --
    if (ray_tracing_fx_.is_enable()) {
      renderer_.buildAccelerationStructures(cmd);    // outside any render pass

      if (auto const view = renderer_.ray_tracing_view(); view.valid()) {
        ray_tracing_fx_.set_view(view);
        ray_tracing_fx_.execute(cmd);

        auto const& src_image = ray_tracing_fx_.image_output();

        /* Blitting the image will change the layout to VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL. */
        renderer_.blitColor(cmd, src_image);

        /* So we need to transition it back before the next frame start. */
        cmd.transitionColorImages(
          {src_image},
          VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        traced = true;
      }
    }

    // -- RASTERIZING --
    if (!traced) {
      auto pass = cmd.beginRendering();
      renderer_.render(pass);
      cmd.endRendering();
    }

    drawUI(cmd);
  }

  void buildUI() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Text("Elapsed time: %.2f ms", delta_time() * 1000.0f);
      ImGui::Separator();

      if (ImGui::CollapsingHeader("Ray Tracing", ImGuiTreeNodeFlags_DefaultOpen)) {
        ray_tracing_fx_.setupUI();
      }
    }
    ImGui::End();
  }

 private:
  ArcBallController arcball_controller_{};
  entt::entity root_{entt::null};
  BasicRayTracingFx ray_tracing_fx_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
