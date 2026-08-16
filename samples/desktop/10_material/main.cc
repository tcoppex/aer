/* -------------------------------------------------------------------------- */
//
//    10 - material
//
//  Where we don't bother and use the internal material & rendering system.
//
/* -------------------------------------------------------------------------- */

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  bool setup() final {
    wm_->set_title("10 - kavalkada materia");

    /* Setup the ArcBall camera. */
    {
      arcball_controller_.set_target(vec3(-1.25f, 0.75f, 0.0f));
      arcball_controller_.set_view(lina::kPi/16.0f, lina::kPi/6.0f);
      arcball_controller_.set_dolly(5.0f);

      camera_.set_controller(&arcball_controller_);
    }

    /* Setup the renderer's skybox. */
    renderer_.skybox().setup(ASSETS_DIR "textures/"
      "rogland_clear_night_2k.hdr"
    );

    /* Fallback background color if the skybox is not rendered. */
    renderer_.set_clear_color({ 0.72f, 0.28f, 0.30f, 1.0f });

    /* Load a glTF Scene. */
    future_scene_ = renderer_.asyncLoadGLTF(ASSETS_DIR "models/"
      "AlphaBlendModeTest.glb"
    );

    return true;
  }

  void buildUI() final {
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
    if (future_scene_.valid()
     && future_scene_.wait_for(0ms) == std::future_status::ready) {
      scene_ = future_scene_.get();
      scene_->uploadToDevice();
      future_scene_ = {};
    }
    if (scene_) {
      scene_->update(camera_, elapsed_time());
    }
  }

  void draw(CommandEncoder const& cmd) final {
    auto pass = cmd.beginRendering();
    {
      /* Skybox. */
      if (auto const& skybox = renderer_.skybox(); skybox.is_valid()) {
        skybox.render(pass, camera_);
      }

      /* Loaded GLTF Scene. */
      if (scene_) {
        scene_->render(pass);
      }
    }
    cmd.endRendering();

    /* User Interface. */
    drawUI(cmd);
  }

 private:
  ArcBallController arcball_controller_{};
  std::future<GLTFScene> future_scene_{};
  GLTFScene scene_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
