/* -------------------------------------------------------------------------- */
//
//    11 - raytracing
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
    LOG_CHECK(push_constant_.frame_buffer_address != 0);
    LOG_CHECK(push_constant_.instance_buffer_address != 0);
    // LOG_CHECK(push_constant_.tlas_address != 0);

    if (push_constant_.accumulation_frame_count < max_accumulation_frame_count_) {
      push_constant_.material_buffer_address = material_storage_buffer_.address; //
      RayTracingFx::execute(cmd);
    }
  }

  void set_frame_buffer_address(VkDeviceAddress const frame_buffer_address) final {
    push_constant_.frame_buffer_address = frame_buffer_address;
  }

  void set_instance_buffer_address(VkDeviceAddress const instance_buffer_address) final {
    push_constant_.instance_buffer_address = instance_buffer_address;
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
      make_modules( backend::ShaderStage::Raygen,     { "raygen.rgen" }),
      make_modules( backend::ShaderStage::AnyHit,     { "anyhit.rahit" }),
      make_modules( backend::ShaderStage::ClosestHit, { "closesthit.rchit" }),
      make_modules( backend::ShaderStage::Miss,       { "miss.rmiss" }),
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

  void buildMaterials(std::vector<scene::MaterialProxy> const& proxy_materials) final {
    if (proxy_materials.empty()) {
      return;
    }

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
        .alpha_cutoff         = proxy.alpha_cutoff,
      });
    }
  }

  void const* material_buffer_data() const final {
    return materials_.data();
  }

  size_t material_buffer_size() const final {
    return materials_.empty()
      ? size_t(0)
      : materials_.size() * sizeof(materials_[0])
      ;
  }

 private:
  int32_t max_accumulation_frame_count_{100};

  mutable shader_interop::PushConstant push_constant_{
    .num_samples      = 8,
    .jitter_factor    = 2.0f,
    .light_intensity  = 40.0f,
    .sky_intensity    = 0.6f,
  };

  std::vector<shader_interop::RayTracingMaterial> materials_{};
};

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  bool setup() final {

    auto const& features = context_.get_features();
    if (!features.ray_tracing_pipeline.rayTracingPipeline) {
      LOGW("This device does not support ray tracing pipeline.");
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
    ray_tracing_fx_.setup(renderer_.surface_size()); //

    /* Load a glTF Scene. */
    std::string gtlf_filename{ASSETS_DIR "models/"
      "CornellBox-Original.gltf"
    };

    future_scene_ = renderer_.asyncLoadGLTF(gtlf_filename);

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
      scene_->setupRayTracingFx(&ray_tracing_fx_);
      scene_->uploadToDevice(
          GPUResources::kUploadFlagBits_BuildRayTracingData
        | GPUResources::kUploadFlagBits_ReleaseHostDataOnUpload
      );
      future_scene_ = {};
    }

    if (scene_) {
      scene_->update(camera_, elapsed_time());
    }

    if (camera_.rebuilt()) {
      ray_tracing_fx_.resetFrameAccumulation();
    }
  }

  void draw(CommandEncoder const& cmd) final {
    if (ray_tracing_fx_.is_enable() && scene_)
    {
      // -- RAY TRACING --

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
    }
    else
    {
      // -- RASTERIZING --

      auto pass = cmd.beginRendering();
      if (scene_) {
        scene_->render(pass);
      }
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
  std::future<GLTFScene> future_scene_{};
  GLTFScene scene_{};

  BasicRayTracingFx ray_tracing_fx_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
