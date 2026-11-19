/* -------------------------------------------------------------------------- */
//
//    10 - material
//
//  Where we don't bother and use the internal rendering system.
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

    /* Fallback background color if the skybox is not rendered. */
    renderer_.set_clear_color({ 0.72f, 0.28f, 0.30f, 1.0f });

    /* Setup the renderer's skybox. */
    renderer_.setSkybox(ASSETS_DIR "textures/rogland_clear_night_2k.hdr");

    /* Load a glTF Scene. */
    renderer_.load(ASSETS_DIR "models/AlphaBlendModeTest.glb");

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

 private:
  ArcBallController arcball_controller_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
