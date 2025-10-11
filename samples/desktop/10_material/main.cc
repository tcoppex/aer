/* -------------------------------------------------------------------------- */
//
//    10 - material
//
//  Where we don't bother and use the internal material & rendering system.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/core/camera.h"
#include "aer/core/arcball_controller.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  bool setup() final {
    wm_->setTitle("10 - kavalkada materia");

    renderer_.set_clear_color({ 0.72f, 0.28f, 0.30f, 0.0f });
    renderer_.skybox().setup(ASSETS_DIR "textures/"
      "rogland_clear_night_2k.hdr"
    );

    /* Setup the ArcBall camera. */
    {
      camera_.setPerspective(
        lina::radians(55.0f),
        viewport_size_.width,
        viewport_size_.height,
        0.01f,
        500.0f
      );
      camera_.setController(&arcball_controller_);

      arcball_controller_.setTarget(vec3(-1.25f, 0.75f, 0.0f));
      arcball_controller_.setView(lina::kPi/16.0f, lina::kPi/6.0f);
      arcball_controller_.setDolly(5.0f);
    }

    /* Load a glTF Scene. */
    std::string gtlf_filename{ASSETS_DIR "models/"
      "AlphaBlendModeTest.glb"
    };

    if constexpr(true) {
      future_scene_ = renderer_.async_load_gltf(gtlf_filename);
    } else {
      scene_ = renderer_.load_gltf(gtlf_filename);
    }

    return true;
  }

  void build_ui() final {
    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Separator();
    }
    ImGui::End();
  }

  void release() final {
    scene_.reset();
  }

  void update(float const dt) final {
    camera_.update(dt);

    if (future_scene_.valid()
     && future_scene_.wait_for(0ms) == std::future_status::ready) {
      scene_ = future_scene_.get();
    }
    if (scene_) {
      scene_->update(camera_, renderer_.surface_size(), elapsed_time());
    }
  }

  void draw() final {
    auto cmd = renderer_.begin_frame();
    {
      auto pass = cmd.begin_rendering();
      {
        // SKYBOX.
        if (auto const& skybox = renderer_.skybox(); skybox.is_valid()) {
          skybox.render(pass, camera_);
        }

        // SCENE.
        if (scene_) {
          scene_->render(pass);
        }
      }
      cmd.end_rendering();

      // UI.
      draw_ui(cmd);
    }
    renderer_.end_frame();
  }

 private:
  Camera camera_{};
  ArcBallController arcball_controller_{};

  std::future<GLTFScene> future_scene_{};
  GLTFScene scene_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
