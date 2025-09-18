/* -------------------------------------------------------------------------- */
//
//    11 - raytracing
//
//
//
/* -------------------------------------------------------------------------- */

#include "framework/application.h"
#include "framework/core/camera.h"
#include "framework/core/arcball_controller.h"

#include "framework/renderer/fx/postprocess/ray_tracing/ray_tracing_fx.h"

namespace shader_interop {
#include "shaders/interop.h"
}

/* -------------------------------------------------------------------------- */

class BasicRayTracingFx : public RayTracingFx {
 public:
  BasicRayTracingFx() = default;

  void resetFrameAccumulation() override {
    push_constant_.accumulation_frame_count = 0u;
  }

  void setupUI() override {
    bool changed = false;

    changed |= ImGui::Checkbox("Enable", &enabled_);


    int value = max_accumulation_frame_count_;
    ImGui::SliderInt(
      "Max Accumulation Frame",
      &value,
      1, 256
    );
    max_accumulation_frame_count_ = value;

    changed |= ImGui::SliderInt(
      "Samples",
      &push_constant_.num_samples,
      1, 16
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

    ImGui::Text("Accumulation frame count: %u", push_constant_.accumulation_frame_count);

    if (changed) {
      resetFrameAccumulation();
    }
  }

  void execute(CommandEncoder& cmd) const override {
    if (push_constant_.accumulation_frame_count < max_accumulation_frame_count_) {
      RayTracingFx::execute(cmd);
    }
  }

 protected:
  backend::ShadersMap createShaderModules() const final {
    auto create_modules{[&](std::vector<std::string_view> const& filenames) {
      return context_ptr_->create_shader_modules(
        COMPILED_SHADERS_DIR,
        filenames
      );
    }};
    return {
      {
        backend::ShaderStage::Raygen,
        create_modules({ "raygen.rgen" })
      },
      {
        backend::ShaderStage::AnyHit,
        create_modules({ "anyhit.rahit" })
      },
      {
        backend::ShaderStage::ClosestHit,
        create_modules({ "closesthit.rchit" })
      },
      {
        backend::ShaderStage::Miss,
        create_modules({ "miss.rmiss" })
      },
    };
  }

  RayTracingPipelineDescriptor_t pipelineDescriptor(backend::ShadersMap const& shaders_map) final {
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
          .generalShader = shader_index("raygen.rgen"),
        }},

        // Miss Groups
        .misses = {{
          .type           = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR,
          .generalShader  = shader_index("miss.rmiss"),
        }},

        // Hit Groups
        .hits = {{
          .type               = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR,
          .closestHitShader   = shader_index("closesthit.rchit"),
          .anyHitShader       = shader_index("anyhit.rahit"),
          .intersectionShader = VK_SHADER_UNUSED_KHR, // only on PROCEDURAL type
        }},
      }
    };
  }

  std::vector<VkPushConstantRange> getPushConstantRanges() const final {
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

  void pushConstant(GenericCommandEncoder const &cmd) const override {
    cmd.push_constant(
      push_constant_,
      pipeline_layout_,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR
      | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
      | VK_SHADER_STAGE_MISS_BIT_KHR
      | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
    );
    push_constant_.accumulation_frame_count += 1u;
  }

  void buildMaterials(std::vector<scene::MaterialProxy> const& proxy_materials) override {
    if (proxy_materials.empty()) {
      return;
    }

    LOG_CHECK(!proxy_materials.empty());
    materials_.reserve(proxy_materials.size());

    // [we should probably sent the material proxy buffer directly to the GPU]
    for (auto const& proxy : proxy_materials) {
      materials_.push_back({
        .emissive_factor      = proxy.emissive_factor,
        .emissive_texture_id  = proxy.bindings.emissive,
        .diffuse_factor       = proxy.pbr_mr.basecolor_factor,
        .diffuse_texture_id   = proxy.bindings.basecolor,
        .orm_texture_id       = proxy.bindings.roughness_metallic,
        .metallic_factor      = proxy.pbr_mr.metallic_factor,
        .roughness_factor     = proxy.pbr_mr.roughness_factor,
        // .normal_texture_id = proxy.bindings.normal,
        // .occlusion_texture_id = proxy.bindings.occlusion,
        .alpha_cutoff = proxy.alpha_cutoff,
        // .double_sided = proxy.double_sided,
      });
    }
  }

  void const* getMaterialBufferData() const override {
    return materials_.data();
  }

  size_t getMaterialBufferSize() const override {
    return !materials_.empty() ? materials_.size() * sizeof(materials_[0])
                               : 0
                               ;
  }

 private:
  uint32_t max_accumulation_frame_count_{64};

  mutable shader_interop::PushConstant push_constant_{
    .num_samples = 4,
    .jitter_factor = 1.0f,
    .light_intensity = 8.0f,
    .sky_intensity = 1.6f,
  };

  std::vector<shader_interop::RayTracingMaterial> materials_{};
};

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  bool setup() final {
    wm_->setTitle("11 - shining through");

    renderer_.set_color_clear_value({ 0.52f, 0.28f, 0.80f, 0.0f });

    /* Setup the ArcBall camera. */
    {
      camera_.setPerspective(
        lina::radians(55.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.1f,
        100.0f
      );
      camera_.setController(&arcball_controller_);

      arcball_controller_.setTarget(vec3f(0.0f, 1.0f, 0.0), false);
      arcball_controller_.setView(0.0f, 0.0f, false);
      arcball_controller_.setDolly(3.5f, false);
    }

    // -------------------------------
    ray_tracing_fx_.init(renderer_);
    ray_tracing_fx_.setup(renderer_.get_surface_size());
    // -------------------------------

    /* Load a glTF Scene. */
    std::string gtlf_filename{ASSETS_DIR "models/"
      "CornellBox-Original.gltf"
    };

    if constexpr(true) {
      future_scene_ = renderer_.async_load_to_device(gtlf_filename);
    } else {
      scene_ = renderer_.load_and_upload(gtlf_filename);
    }

    return true;
  }

  void release() final {
    ray_tracing_fx_.release();
    scene_.reset();
  }

  void update(float const dt) final {
    if (future_scene_.valid()
     && future_scene_.wait_for(0ms) == std::future_status::ready) {
      scene_ = future_scene_.get();
      // -------------------------------
      scene_->set_ray_tracing_fx(&ray_tracing_fx_);
      // -------------------------------
    }

    if (camera_.update(dt)) {
      ray_tracing_fx_.resetFrameAccumulation();
    }

    if (scene_) {
      scene_->update(camera_, renderer_.get_surface_size(), elapsed_time());
    }
  }

  void draw() final {
    auto cmd = renderer_.begin_frame();

    if (ray_tracing_fx_.enabled())
    {
      // RAY TRACER
      ray_tracing_fx_.execute(cmd);
      cmd.blit(ray_tracing_fx_, renderer_);
    }
    else
    {
      // RASTERIZER
      auto pass = cmd.begin_rendering();
      if (scene_) scene_->render(pass);
      cmd.end_rendering();
    }

    /* Draw UI on top. */
    cmd.render_ui(renderer_);

    renderer_.end_frame();
  }

  void build_ui() final {
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
  Camera camera_{};
  ArcBallController arcball_controller_{};

  BasicRayTracingFx ray_tracing_fx_{};

  std::future<GLTFScene> future_scene_{};
  GLTFScene scene_{};
};

// ----------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  return SampleApp().run();
}

/* -------------------------------------------------------------------------- */
