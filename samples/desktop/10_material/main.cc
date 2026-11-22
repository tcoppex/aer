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
    renderer_.set_clear_color({ 0.48f, 0.06f, 0.07f, 1.0f });

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

      /* Display transform. */
      float ev = renderer_.exposure();
      if (ImGui::SliderFloat("Exposure (EV)", &ev, -6.0f, 6.0f)) {
        renderer_.set_exposure(ev);
      }

      struct Operator { char const* name; Tonemapping value; };
      static constexpr Operator kOperators[] = {
        { "None",           Tonemapping::None },
        { "Linear",         Tonemapping::Linear },
        { "Reinhard",       Tonemapping::SimpleReinhard },
        { "Luma Reinhard",  Tonemapping::LumaReinhard },
        { "ACES",           Tonemapping::FilmicACES },
        { "Uncharted 2",    Tonemapping::Uncharted2 },
        { "PBR Neutral",    Tonemapping::PBRNeutral },
      };

      auto const current = renderer_.tonemapping();
      char const* label = "-";
      for (auto const& op : kOperators) {
        if (op.value == current) {
          label = op.name;
        }
      }
      if (ImGui::BeginCombo("Tonemapping", label)) {
        for (auto const& op : kOperators) {
          if (ImGui::Selectable(op.name, op.value == current)) {
            renderer_.set_tonemapping(op.value);
          }
        }
        ImGui::EndCombo();
      }

      /* Background only brightness (the lighting is not affected). */
      float sky_intensity = renderer_.skybox().hdr_intensity();
      if (ImGui::SliderFloat("Sky intensity", &sky_intensity, 0.0f, 4.0f)) {
        renderer_.skybox().set_hdr_intensity(sky_intensity);
      }
    }
    ImGui::End();
  }

 private:
  ArcBallController arcball_controller_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
