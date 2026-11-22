/* -------------------------------------------------------------------------- */
//
//    15 - skinning
//
//  Where we puppeteer an animated character.
//
/* -------------------------------------------------------------------------- */

#include <cstdio>
#include <string>
#include <vector>

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  struct Character {
    entt::entity root{entt::null};
    int   clip{0};       // index in clip_names_
    float speed{1.0f};
    bool  loop{true};
  };

 private:
  bool setup() final {
    wm_->set_title("15 - skinning");

    /* Setup the ArcBall camera (a human sized character is expected). */
    {
      arcball_controller_.set_target(vec3(0.0f, 1.0f, 0.0f));
      arcball_controller_.set_view(lina::kPi/16.0f, lina::kPi/6.0f);
      arcball_controller_.set_dolly(4.0f);
      camera_.set_controller(&arcball_controller_);
    }

    renderer_.set_clear_color({ 0.16f, 0.14f, 0.12f, 1.0f });
    // renderer_.setSkybox(ASSETS_DIR "textures/qwantani_dusk_2_puresky_2k.hdr");

    /* Load the asset once, instances share it. */
    asset_ = renderer_.loadAsset(ASSETS_DIR "models/genan_m.glb");
    if (!asset_.valid()) {
      return false;
    }

    addCharacter();
    return !characters_.empty();
  }

  void buildUI() final {
    auto& animation = renderer_.animation();

    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Separator();

      /* Instances share the asset but animate independently. */
      if (ImGui::Button("Add instance")) {
        addCharacter();
      }
      if (characters_.size() > 1u) {
        ImGui::SameLine();
        ImGui::SliderInt("Instance", &current_, 0, static_cast<int>(characters_.size()) - 1);
      }
      current_ = std::clamp(current_, 0, static_cast<int>(characters_.size()) - 1);
      auto& c = characters_[current_];

      ImGui::Separator();

      if (clip_names_.empty()) {
        ImGui::TextUnformatted("No animation clip in this asset.");
      } else {
        /* Clip selection (select, then trigger). */
        if (ImGui::BeginCombo("Clip", clip_names_[c.clip].c_str())) {
          for (int i = 0; i < static_cast<int>(clip_names_.size()); ++i) {
            bool const selected = (i == c.clip);
            if (ImGui::Selectable(clip_names_[i].c_str(), selected)) {
              c.clip = i;
            }
            if (selected) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        auto const state = animation.state(c.root);

        if (ImGui::Button("Play")) {
          play(c);
        }
        ImGui::SameLine();
        if (ImGui::Button(state.playing ? "Pause" : "Resume")) {
          if (state.playing) {
            animation.pause(c.root);
          } else {
            animation.resume(c.root);
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
          animation.stop(c.root);
        }

        if (ImGui::SliderFloat("Speed", &c.speed, 0.0f, 4.0f, "%.2f")) {
          animation.setSpeed(c.root, c.speed);
        }
        if (ImGui::Checkbox("Loop", &c.loop)) {
          animation.setLoop(c.root, c.loop);
        }

        if (!state.clip.empty()) {
          char overlay[64];
          std::snprintf(overlay, sizeof(overlay), "%.2f / %.2f s", state.time, state.duration);
          ImGui::ProgressBar(
            (state.duration > 0.0f) ? (state.time / state.duration) : 0.0f,
            ImVec2(-1.0f, 0.0f),
            overlay
          );
        }
      }
    }
    ImGui::End();
  }

 private:
  void addCharacter() {
    /* Side by side. */
    auto const placement = lina::translation_matrix(
      vec3(1.5f * static_cast<float>(characters_.size()), 0.0f, 0.0f)
    );
    auto const root = renderer_.createInstance(asset_, entt::null, placement);
    if (root == entt::null) {
      return;
    }

    if (clip_names_.empty()) {
      clip_names_ = renderer_.animation().clipNames(root);
    }

    characters_.push_back({ .root = root });
    if (!clip_names_.empty()) {
      play(characters_.back()); // (the first clip)
    }
  }

  void play(Character const& c) {
    renderer_.animation().play(c.root, clip_names_[c.clip], c.loop);
  }

 private:
  ArcBallController arcball_controller_{};

  AssetHandle asset_{};
  std::vector<Character> characters_{};
  std::vector<std::string> clip_names_{};
  int current_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */