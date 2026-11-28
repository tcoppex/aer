/* -------------------------------------------------------------------------- */
//
//    15 - skinning
//
//  Where we puppeteer an animated character.
//
/* -------------------------------------------------------------------------- */

#include <algorithm>
#include <string>
#include <vector>

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "aer/scene/ecs/animation_components.h"
#include "aer/scene/ecs/animation_system.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  // ---------------------------------------------------------------------------
  // Types
  // ---------------------------------------------------------------------------

  struct IKEntry {
    scene::IKId id{};
    std::string end{};
    vec3  target{};
    float weight{1.0f};
    vec3  pole{};
    bool  use_pole{false};
    bool  foot{false};
  };

  struct LookEntry {
    scene::LookId id{};
    std::string head{};
    vec3  target{};
    float weight{1.0f};
  };

  struct ChainUI {
    scene::ChainIKId id{};
    int   end{0};
    int   bones{3};
    vec3  target{};
    float weight{1.0f};
    bool  use_pole{false};
    vec3  pole{};
    float root_limit{30.0f};    // degrees, the root bone (shoulder assist)
    float joint_limit{150.0f};  // degrees, the other bones
    bool  hinge{true};          // last joint (elbow)
  };

  struct Character {
    entt::entity root{entt::null};
    int   clip{0};
    float speed{1.0f};
    bool  loop{true};
    float fade{0.25f};

    float x{0.0f};
    float z{0.0f};

    std::vector<IKEntry>   ik{};
    std::vector<LookEntry> look{};

    bool  foot_ik{false};
    float foot_ankle{-1.0f};
  };

  struct AxisUI {
    char const* name;
    vec3 axis;
  };

  struct ParamUI {
    char const* name;
    float min;
    float max;
  };

  // ---------------------------------------------------------------------------
  // Constants
  // ---------------------------------------------------------------------------

  static constexpr vec3 kNodeColor  {1.0f, 0.70f, 0.15f};
  static constexpr vec3 kLeafColor  {0.20f, 0.90f, 0.40f};
  static constexpr vec3 kTargetColor{1.0f, 0.25f, 0.85f};
  static constexpr vec3 kPoleColor  {0.20f, 0.85f, 1.0f};
  static constexpr vec3 kGroundColor{0.22f, 0.22f, 0.26f};
  static constexpr vec3 kLookColor  {1.0f, 0.90f, 0.20f};

  static constexpr float kBoneThickness   = 2.0f;
  static constexpr float kTargetThickness = 2.0f;
  static constexpr float kGroundThickness = 1.0f;
  static constexpr float kTargetSize      = 0.06f;

  static constexpr AxisUI kAxes[] = {
    { "+Z", { 0.0f, 0.0f,  1.0f} }, { "-Z", { 0.0f,  0.0f, -1.0f} },
    { "+Y", { 0.0f, 1.0f,  0.0f} }, { "-Y", { 0.0f, -1.0f,  0.0f} },
    { "+X", { 1.0f, 0.0f,  0.0f} }, { "-X", {-1.0f,  0.0f,  0.0f} },
  };

  static constexpr ParamUI kParams[] = {
    { "go",    0.0f, 1.0f },   // A <-> B
    { "mix",   0.0f, 1.0f },   // any -> Blend
    { "blend", 0.0f, 1.0f },   // blend tree parameter (clip 0 .. clip 1)
  };

  // Clips used by the demo graph's kick states.
  static constexpr size_t kKick1Clip = 59u;
  static constexpr size_t kKick2Clip = 60u;

 private:
  // ---------------------------------------------------------------------------
  // Terrain
  // ---------------------------------------------------------------------------

  static
  float GroundHeight(float x, float z) {
    auto smooth = [](float a, float b, float v) {
      float const t = std::clamp((v - a) / (b - a), 0.0f, 1.0f);
      return t * t * (3.0f - 2.0f * t);
    };
    return 0.25f * smooth(0.30f, 0.50f, x) + 0.30f * smooth(-1.0f, 1.0f, z);
  }

  static
  bool QueryGround(vec3 const& origin, float max_distance, scene::GroundHit& hit) {
    float const h = GroundHeight(origin.x, origin.z);
    float const d = origin.y - h;
    if ((d < 0.0f) || (d > max_distance)) {
      return false;
    }
    float const e = 0.01f;
    float const dx = (GroundHeight(origin.x + e, origin.z) - GroundHeight(origin.x - e, origin.z)) / (2.0f * e);
    float const dz = (GroundHeight(origin.x, origin.z + e) - GroundHeight(origin.x, origin.z - e)) / (2.0f * e);
    hit.position = vec3(origin.x, h, origin.z);
    hit.normal = lina::normalize(vec3(-dx, 1.0f, -dz));
    return true;
  }

  static
  std::vector<scene::AnimationSystem::ChainLimitDesc> makeChainLimits(ChainUI const& ui) {
    using Limit = scene::AnimationSystem::ChainLimitDesc;
    std::vector<Limit> limits(static_cast<size_t>(ui.bones), Limit{ .max_angle = ui.joint_limit });
    limits.front().max_angle = ui.root_limit;
    limits.back().hinge = ui.hinge;
    return limits;
  }

  // ---------------------------------------------------------------------------
  // UI helpers
  // ---------------------------------------------------------------------------

  /* Combo box over a list of names, the index is clamped. */
  static
  bool Combo(char const* label, std::vector<std::string> const& items, int& index) {
    if (items.empty()) {
      return false;
    }
    index = std::clamp(index, 0, static_cast<int>(items.size()) - 1);

    bool changed = false;
    if (ImGui::BeginCombo(label, items[index].c_str())) {
      for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        bool const selected = (i == index);
        if (ImGui::Selectable(items[i].c_str(), selected)) {
          index = i;
          changed = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  }

  static
  bool AxisCombo(char const* label, int& index) {
    bool changed = false;
    if (ImGui::BeginCombo(label, kAxes[index].name)) {
      for (int i = 0; i < static_cast<int>(std::size(kAxes)); ++i) {
        bool const selected = (i == index);
        if (ImGui::Selectable(kAxes[i].name, selected)) {
          index = i;
          changed = true;
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    return changed;
  }

  static
  bool Section(char const* title) {
    return ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen);
  }

  static
  bool FadeButtons(float& weight, float& target_weight) {
    bool pressed = false;
    if (ImGui::Button("Fade out")) {
      weight = target_weight = 0.0f;
      pressed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Fade in")) {
      weight = target_weight = 1.0f;
      pressed = true;
    }
    return pressed;
  }

 private:
  // ---------------------------------------------------------------------------
  // Application
  // ---------------------------------------------------------------------------

  bool setup() final {
    wm_->set_title("15 - skinning");

    /* Setup the ArcBall camera. */
    arcball_controller_.set_target(vec3(0.0f, 1.0f, 0.0f));
    arcball_controller_.set_view(lina::kPi / 16.0f, lina::kPi / 6.0f);
    arcball_controller_.set_dolly(4.0f);
    camera_.set_controller(&arcball_controller_);

    renderer_.set_clear_color({ 0.02f, 0.02f, 0.01f, 1.0f });
    renderer_.setSkybox(ASSETS_DIR "textures/qwantani_dusk_2_puresky_2k.hdr");

    /* Ground query used by the foot IK. */
    renderer_.animation().setGroundQuery(
      [](vec3 const& origin, float max_distance, scene::GroundHit& hit) {
        return QueryGround(origin, max_distance, hit);
      }
    );

    /* Load the asset once, instances share it. */
    asset_ = renderer_.loadAsset(ASSETS_DIR "models/"
      // ---------------------
      // Hands    : joints 28 / 29
      // Foot     : joints 3 / 6
      // Idle     : clips 0-18
      // Walking  : clips 19-36
      // Running  : clips 37-49
      // Picking  : clips 57-58
      // Kicks    : clips 59-61
      // ---------------------
      "genan_fixed.glb"      // static + fixed root position
      // "genan_m.glb"       // root motion
      // "sintel_anim.glb"
    );
    if (!asset_.valid()) {
      return false;
    }

    addCharacter();
    return !characters_.empty();
  }

  void update(float const /*dt*/) final {
    auto& debug = renderer_.debug_draw();

    if (show_ground_) {
      drawGround(debug);
    }
    if (show_skeleton_) {
      drawSkeleton(debug);
    }
    drawIKTargets(debug);
    drawChainTargets(debug);
    drawLookTargets(debug);
  }

  void buildUI() final {
    ImGui::Begin("Settings");

    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::Separator();

    buildInstanceUI();
    auto& c = characters_[current_];

    if (clip_names_.empty()) {
      ImGui::TextUnformatted("No animation clip in this asset.");
    } else {
      auto const state = renderer_.animation().state(c.root);

      buildPlaybackUI(c, state);
      buildGraphUI(c, state);
      buildIKUI(c, state);
      buildChainUI(c);
      buildLookUI(c);
    }

    buildDisplayUI();
    buildMorphUI();

    ImGui::End();
  }

 private:
  // ---------------------------------------------------------------------------
  // Animation graph
  // ---------------------------------------------------------------------------

  bool setupAnimationGraph(entt::entity root) {
    if (clip_names_.size() < 2u) {
      return false;
    }

    /* Built once, then shared (the system compiles it once per skeleton). */
    if (!graph_) {
      using Op = scene::AnimationStateMachine::Condition::Op;

      auto desc = scene::AnimationGraphDesc{
        .name = "demo",
        .parameters = {
          {"go"}, {"mix"}, {"blend"},
          { .name = "hit", .trigger = true, .expiry = 1.0f }
        },
        .trees = {
          {
            .name = "blend",
            .parameter = "blend",
            .entries = { {clip_names_[0], 0.0f}, {clip_names_[1], 1.0f} },
          },
        },
        .machine = {
          .name = "demo",
          .states = {
            { .name = "A",     .motion = clip_names_[0] },
            { .name = "B",     .motion = clip_names_[1] },
            { .name = "Blend", .motion = "blend" },
          },
          .transitions = {
            { .from = "",      .to = "Blend", .conditions = {{"mix", Op::Greater, 0.5f}}, .fade = 0.3f },
            { .from = "Blend", .to = "A",     .conditions = {{"mix", Op::Less,    0.5f}}, .fade = 0.3f },
            { .from = "A",     .to = "B",     .conditions = {{"go",  Op::Greater, 0.5f}}, .fade = 0.3f },
            { .from = "B",     .to = "A",     .conditions = {{"go",  Op::Less,    0.5f}}, .fade = 0.3f },
          },
        },
      };

      /* Kick combo (A -> Kick1 -> Kick2 -> A), only if the asset has the clips. */
      if (clip_names_.size() > kKick2Clip) {
        desc.machine.states.push_back({ .name = "Kick1", .motion = clip_names_[kKick1Clip], .loop = false });
        desc.machine.states.push_back({ .name = "Kick2", .motion = clip_names_[kKick2Clip], .loop = false });

        desc.machine.transitions.push_back({ .from = "A",     .to = "Kick1", .conditions = {{"hit", Op::Trigger}}, .fade = 0.1f });
        desc.machine.transitions.push_back({ .from = "Kick1", .to = "Kick2", .conditions = {{"hit", Op::Trigger}}, .exit_time = 0.6f, .fade = 0.15f });
        desc.machine.transitions.push_back({ .from = "Kick1", .to = "A", .on_finish = true, .fade = 0.2f });
        desc.machine.transitions.push_back({ .from = "Kick2", .to = "A", .on_finish = true, .fade = 0.2f });
      }
      graph_ = std::make_shared<scene::AnimationGraphDesc>(std::move(desc));
    }
    return renderer_.animation().setGraph(root, graph_);
  }

  // ---------------------------------------------------------------------------
  // UI sections
  // ---------------------------------------------------------------------------

  void buildInstanceUI() {
    /* Instances share the asset but animate independently. */
    if (ImGui::Button("Add instance")) {
      addCharacter();
    }
    if (characters_.size() > 1u) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::SliderInt("##instance", &current_, 0, static_cast<int>(characters_.size()) - 1, "Instance %d");
    }
    current_ = std::clamp(current_, 0, static_cast<int>(characters_.size()) - 1);
  }

  void buildPlaybackUI(Character& c, scene::AnimationSystem::State const& state) {
    if (!Section("Playback")) {
      return;
    }
    auto& animation = renderer_.animation();

    Combo("Clip", clip_names_, c.clip);
    ImGui::SliderFloat("Fade (s)", &c.fade, 0.0f, 2.0f, "%.2f");

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

    if (ImGui::SliderFloat("Speed", &c.speed, -4.0f, 4.0f, "%.2f")) {
      animation.setSpeed(c.root, c.speed);
    }
    if (ImGui::Checkbox("Loop", &c.loop)) {
      animation.setLoop(c.root, c.loop);
    }

    if (!state.clip.empty()) {
      float const progress = (state.duration > 0.0f) ? (state.time / state.duration) : 0.0f;
      char label[64];
      std::snprintf(label, sizeof(label), "%.2f / %.2f s", state.time, state.duration);
      ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), label);

      if (!state.playing) {
        float t = state.time;
        if (ImGui::SliderFloat("Time", &t, 0.0f, state.duration, "%.2f s")) {
          animation.setTime(c.root, t);
        }
      }
    }
  }

  void buildGraphUI(Character const& c, scene::AnimationSystem::State const& state) {
    auto& animation = renderer_.animation();

    auto const states = animation.stateNames(c.root);
    if (states.empty() || !Section("Animation graph")) {
      return;
    }

    for (auto const& p : kParams) {
      float value = animation.parameter(c.root, p.name);
      if (ImGui::SliderFloat(p.name, &value, p.min, p.max, "%.2f")) {
        animation.setParameter(c.root, p.name, value);
      }
    }

    /* The current state is highlighted, a click forces a transition. */
    ImGui::TextUnformatted("States");
    float const avail = ImGui::GetContentRegionAvail().x;
    float used = 0.0f;
    for (auto const& name : states) {
      bool const current = (state.machine_state == name);

      float const w = ImGui::CalcTextSize(name.c_str()).x + 2.0f * ImGui::GetStyle().FramePadding.x;
      if (used > 0.0f && used + w > avail) {
        used = 0.0f;
      } else if (used > 0.0f) {
        ImGui::SameLine();
      }
      used += w + ImGui::GetStyle().ItemSpacing.x;

      ImGui::PushID(name.c_str());
      if (current) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      }
      if (ImGui::Button(name.c_str())) {
        animation.setState(c.root, name, c.fade);
      }
      if (current) {
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
    }

    if (ImGui::Button("Hit (trigger)")) {
      animation.setTrigger(c.root, "hit");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", (animation.parameter(c.root, "hit") > 0.0f) ? "pending" : "-");

    if (state.machine_state.empty()) {
      ImGui::TextDisabled("Machine inactive (manual control). Click a state to re-enter.");
    } else if (state.fade < 1.0f) {
      ImGui::ProgressBar(state.fade, ImVec2(-1.0f, 0.0f), "fade");
    }
  }

  void buildIKUI(Character& c, scene::AnimationSystem::State const& state) {
    if (joint_names_.empty() || !Section("Two-bone IK")) {
      return;
    }
    auto& animation = renderer_.animation();

    /* The chain is the end joint and its two parents. */
    Combo("joint", joint_names_, ik_joint_);
    ImGui::SameLine();
    if (ImGui::Button("Add##ik")) {
      auto const& end = joint_names_[ik_joint_];
      auto const id = animation.addTwoBoneIK(c.root, { .end = end });
      if (id.valid()) {
        c.ik.push_back({
          .id = id,
          .end = end,
          .target = animation.ikTarget(c.root, id),
          .pole = animation.ikPole(c.root, id),
        });
      }
    }

    if (!c.ik.empty()) {
      ImGui::SliderFloat("IK fade (s)", &ik_fade_, 0.0f, 1.0f, "%.2f");
      ImGui::Checkbox("Free during Kick", &ik_free_on_kick_);
    }

    for (size_t i = 0; i < c.ik.size(); ++i) {
      auto& entry = c.ik[i];
      bool remove = false;

      ImGui::PushID(static_cast<int>(i));
      if (ImGui::TreeNodeEx(entry.end.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
        if (c.foot_ik && entry.foot) {
          ImGui::TextDisabled("Target driven by foot IK");
        } else if (ImGui::DragFloat3("Target", &entry.target.x, 0.01f)) {
          animation.setIKTarget(c.root, entry.id, entry.target);
        }

        if (ImGui::Checkbox("Pole", &entry.use_pole)) {
          if (entry.use_pole) {
            animation.setIKPole(c.root, entry.id, entry.pole);
          } else {
            animation.clearIKPole(c.root, entry.id);
          }
        }
        if (entry.use_pole) {
          ImGui::SameLine();
          if (ImGui::DragFloat3("##pole", &entry.pole.x, 0.01f)) {
            animation.setIKPole(c.root, entry.id, entry.pole);
          }
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Foot", &entry.foot)) {
          foot_dirty_ = true;
        }

        if (ImGui::SliderFloat("Weight", &entry.weight, 0.0f, 1.0f, "%.2f")) {
          animation.setIKWeight(c.root, entry.id, entry.weight);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%.2f)", animation.ikWeight(c.root, entry.id));

        float fade_weight = entry.weight;
        if (FadeButtons(fade_weight, entry.weight)) {
          animation.setIKWeight(c.root, entry.id, fade_weight, ik_fade_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
          remove = true;
        }

        /* Nested in the entry, so it reads as part of this bone. */
        if (entry.foot) {
          buildFootIKUI(c);
        }
        ImGui::TreePop();
      }
      ImGui::PopID();

      if (remove) {
        animation.removeIK(c.root, entry.id);
        c.ik.erase(c.ik.begin() + static_cast<std::ptrdiff_t>(i));
        foot_dirty_ = true;
        break;
      }
    }

    /* Gameplay style: repeated every frame, the fade only starts on a change. */
    if (ik_free_on_kick_) {
      bool const kicking = state.machine_state.starts_with("Kick");
      for (auto const& entry : c.ik) {
        animation.setIKWeight(c.root, entry.id, kicking ? 0.0f : entry.weight, ik_fade_);
      }
    }
  }

  void buildChainUI(Character const& c) {
    if (!Section("Chain IK (FABRIK)")) {
      return;
    }
    auto& animation = renderer_.animation();
    auto& ui = chains_[c.root];

    if (!ui.id.valid()) {
      auto const names = animation.twoBoneIKEndJoints(c.root);
      if (names.empty()) {
        return;
      }
      Combo("Chain end", names, ui.end);
      ImGui::SliderInt("Bones", &ui.bones, 2, 6);

      if (ImGui::Button("Add chain")) {
        ui.id = animation.addChainIK(c.root, {
          .end = names[ui.end],
          .length = static_cast<uint32_t>(ui.bones),
          .limits = makeChainLimits(ui),
        });
        if (ui.id.valid()) {
          ui.target = animation.chainIKTarget(c.root, ui.id);
          ui.weight = 1.0f;
          ui.use_pole = false;
        }
      }
      return;
    }

    if (ImGui::DragFloat3("Target", &ui.target.x, 0.01f)) {
      animation.setChainIKTarget(c.root, ui.id, ui.target);
    }
    if (ImGui::SliderFloat("Weight", &ui.weight, 0.0f, 1.0f, "%.2f")) {
      animation.setChainIKWeight(c.root, ui.id, ui.weight);
    }
    if (ImGui::Checkbox("Pole", &ui.use_pole)) {
      if (ui.use_pole) {
        animation.setChainIKPole(c.root, ui.id, ui.pole);
      } else {
        animation.clearChainIKPole(c.root, ui.id);
      }
    }
    if (ui.use_pole) {
      ImGui::SameLine();
      if (ImGui::DragFloat3("##chain_pole", &ui.pole.x, 0.01f)) {
        animation.setChainIKPole(c.root, ui.id, ui.pole);
      }
    }

    bool limits_changed = false;
    limits_changed |= ImGui::SliderFloat("Root limit", &ui.root_limit, 0.0f, 180.0f, "%.0f deg");
    limits_changed |= ImGui::SliderFloat("Joint limit", &ui.joint_limit, 0.0f, 180.0f, "%.0f deg");
    limits_changed |= ImGui::Checkbox("Hinge (last joint)", &ui.hinge);
    if (limits_changed) {
      animation.setChainIKLimits(c.root, ui.id, makeChainLimits(ui));
    }

    if (ImGui::Button("Remove chain")) {
      animation.removeChainIK(c.root, ui.id);
      ui.id = {};
    }
  }

  void buildFootIKUI(Character& c) {
    auto& animation = renderer_.animation();

    bool changed = foot_dirty_;
    foot_dirty_ = false;

    if (ImGui::TreeNodeEx("Foot IK / ground", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::Checkbox("Show ground", &show_ground_);

      /* The root follows the terrain, as a character controller would. */
      bool moved = ImGui::SliderFloat("X", &c.x, -2.0f, 2.0f, "%.2f");
      moved |= ImGui::SliderFloat("Z", &c.z, -2.0f, 2.0f, "%.2f");
      if (moved) {
        placeCharacter(c);
      }

      changed |= ImGui::Checkbox("Enable foot IK", &c.foot_ik);
      if (c.foot_ik) {
        if (c.foot_ankle >= 0.0f) {
          changed |= ImGui::SliderFloat("Ankle height", &c.foot_ankle, 0.0f, 0.3f, "%.3f");
        }
        if (ImGui::Button("Measure ankle height")) {
          c.foot_ankle = -1.0f;   // from the current pose
          changed = true;
        }
        changed |= ImGui::SliderFloat("Probe range", &foot_probe_, 0.1f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Blend height", &foot_blend_, 0.02f, 0.5f, "%.2f");
        changed |= ImGui::SliderFloat("Smoothing", &foot_smooth_, 0.0f, 30.0f, "%.1f");
        changed |= ImGui::Checkbox("Align feet to ground", &foot_align_);
        if (foot_align_) {
          changed |= ImGui::SliderFloat("Max foot angle", &foot_max_angle_, 0.0f, 60.0f, "%.0f deg");
        }
      }
      ImGui::TreePop();
    }
    if (!changed) {
      return;
    }

    std::vector<scene::IKId> feet{};
    for (auto const& entry : c.ik) {
      if (entry.foot) {
        feet.push_back(entry.id);
      }
    }

    bool active = false;
    if (c.foot_ik && !feet.empty()) {
      active = animation.setFootIK(c.root, {
        .feet = feet,
        .params = {
          .ankle_height = c.foot_ankle,
          .probe_height = foot_probe_,
          .probe_depth = foot_probe_,
          .blend_height = foot_blend_,
          .smooth = foot_smooth_,
          .align = foot_align_,
          .max_angle = foot_max_angle_,
        }
      });
    }

    if (active) {
      c.foot_ankle = animation.footIKAnkleHeight(c.root);
    } else {
      animation.clearFootIK(c.root);
      for (auto& entry : c.ik) {
        if (entry.foot) {
          entry.target = animation.ikTarget(c.root, entry.id);   // where the rig left it
        }
      }
      if (!feet.empty()) {
        c.foot_ik = false;   // setFootIK failed
      }
    }
  }

  void buildLookUI(Character& c) {
    if (joint_names_.empty() || !Section("Look-at")) {
      return;
    }
    auto& animation = renderer_.animation();

    Combo("Head joint", joint_names_, look_joint_);
    ImGui::SliderInt("Chain length", &look_length_, 1, 4);
    AxisCombo("Aim axis", look_axis_);
    ImGui::SliderFloat("Max angle", &look_max_angle_, 10.0f, 120.0f, "%.0f deg");
    ImGui::SliderFloat("Fade zone", &look_fade_angle_, 5.0f, 90.0f, "%.0f deg");
    ImGui::SliderFloat("Smoothing", &look_smooth_, 0.0f, 30.0f, "%.1f");

    if (ImGui::Button("Add look-at")) {
      auto const& head = joint_names_[look_joint_];
      auto const id = animation.addLookAt(c.root, {
        .head = head,
        .length = static_cast<uint32_t>(look_length_),
        .params = {
          .aim        = kAxes[look_axis_].axis,
          .max_angle  = look_max_angle_,
          .fade_angle = look_fade_angle_,
          .smooth     = look_smooth_,
        }
      });
      if (id.valid()) {
        c.look.push_back({ .id = id, .head = head, .target = animation.lookTarget(c.root, id) });
      }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Orbit target", &look_orbit_);

    for (size_t i = 0; i < c.look.size(); ++i) {
      auto& entry = c.look[i];
      bool remove = false;

      ImGui::PushID("look");
      ImGui::PushID(static_cast<int>(i));
      ImGui::SeparatorText(entry.head.c_str());

      if (look_orbit_) {
        /* Around the head, it goes behind the character too (to see the limits). */
        if (auto const aim = animation.lookAim(c.root, entry.id)) {
          float const t = static_cast<float>(ImGui::GetTime());
          entry.target = aim->origin + vec3(
            1.5f * std::sin(t), 0.3f * std::sin(1.7f * t), 1.5f * std::cos(t)
          );
          animation.setLookTarget(c.root, entry.id, entry.target);
        }
      } else if (ImGui::DragFloat3("Target", &entry.target.x, 0.01f)) {
        animation.setLookTarget(c.root, entry.id, entry.target);
      }

      if (ImGui::SliderFloat("Weight", &entry.weight, 0.0f, 1.0f, "%.2f")) {
        animation.setLookWeight(c.root, entry.id, entry.weight);
      }

      float fade_weight = entry.weight;
      if (FadeButtons(fade_weight, entry.weight)) {
        animation.setLookWeight(c.root, entry.id, fade_weight, ik_fade_);
      }
      ImGui::SameLine();
      if (ImGui::Button("Remove")) {
        remove = true;
      }
      ImGui::PopID();
      ImGui::PopID();

      if (remove) {
        animation.removeLookAt(c.root, entry.id);
        c.look.erase(c.look.begin() + static_cast<std::ptrdiff_t>(i));
        break;
      }
    }
  }

  void buildDisplayUI() {
    if (!Section("Display")) {
      return;
    }

    ImGui::Checkbox("Skeleton", &show_skeleton_);
    if (show_skeleton_) {
      ImGui::Indent();
      ImGui::SliderFloat("bone thickness (px)", &bone_thickness_, 0.5f, 4.0f, "%.1f");
      ImGui::Checkbox("Joint markers", &show_markers_);
      if (show_markers_) {
        ImGui::SliderFloat("Marker size", &marker_size_, 0.005f, 0.1f, "%.3f");
        ImGui::SliderFloat("joint thickness (px)", &joint_thickness_, 0.5f, 8.0f, "%.1f");
      }
      ImGui::Unindent();
    }

    if (ImGui::Checkbox("Dual quaternion skinning", &use_dqs_)) {
      using namespace scene::component;
      auto const mode = use_dqs_ ? scene::SkinningMode::DualQuaternion : scene::SkinningMode::Linear;
      renderer_.scene().registry.view<Skin>().each([mode](Skin& skin) {
        skin.mode = mode;
      });
    }
  }

  void buildMorphUI() {
    using namespace scene::component;
    auto& reg = renderer_.scene().registry;
    if (reg.view<MorphWeights>().empty() || !ImGui::CollapsingHeader("Morph targets")) {
      return;
    }

    reg.view<MorphWeights>().each([](entt::entity e, MorphWeights& m) {
      ImGui::PushID(static_cast<int>(entt::to_integral(e)));
      size_t const shown = std::min<size_t>(m.weights.size(), 32u);
      for (size_t t = 0; t < shown; ++t) {
        auto const label = "Target " + std::to_string(t);
        ImGui::SliderFloat(label.c_str(), &m.weights[t], 0.0f, 1.0f, "%.2f");
      }
      if (ImGui::Button("Reset weights")) {
        std::ranges::fill(m.weights, 0.0f);
      }
      ImGui::PopID();
    });
  }

  // ---------------------------------------------------------------------------
  // Characters
  // ---------------------------------------------------------------------------

  void addCharacter() {
    auto const ground_x = 1.5f * static_cast<float>(characters_.size());
    auto const placement = lina::translation_matrix(vec3(ground_x, 0.0f, 0.0f));
    auto const root = renderer_.createInstance(asset_, entt::null, placement);
    if (root == entt::null) {
      return;
    }

    if (clip_names_.empty()) {
      clip_names_ = renderer_.animation().clipNames(root);
    }
    if (joint_names_.empty()) {
      joint_names_ = renderer_.animation().twoBoneIKEndJoints(root);
    }

    characters_.push_back({
      .root = root,
      .x = ground_x,
    });
    placeCharacter(characters_.back());

    if (!setupAnimationGraph(root) && !clip_names_.empty()) {
      play(characters_.back());
    }
  }

  void placeCharacter(Character const& c) {
    auto& reg = renderer_.scene().registry;
    reg.get<scene::component::Transform>(c.root).translation =
      vec3(c.x, GroundHeight(c.x, c.z), c.z);
  }

  void play(Character const& c) {
    renderer_.animation().play(c.root, clip_names_[c.clip], c.loop, c.fade);
  }

  // ---------------------------------------------------------------------------
  // Debug draw
  // ---------------------------------------------------------------------------

  using Depth = DebugDraw::Depth;

  /* World space cross, drawn over the mesh. */
  static
  void drawCross(DebugDraw& debug, vec3 const& p, vec3 const& color) {
    debug.cross(p, kTargetSize, color, kTargetThickness);
  }

  void drawGround(DebugDraw& debug) {
    constexpr int kLines = 21;
    constexpr int kSegments = 80;
    constexpr float kExtent = 2.0f;
    constexpr float kStep = 2.0f * kExtent / static_cast<float>(kLines - 1);
    constexpr float kSegment = 2.0f * kExtent / static_cast<float>(kSegments);

    for (int i = 0; i < kLines; ++i) {
      float const a = -kExtent + kStep * static_cast<float>(i);
      for (int s = 0; s < kSegments; ++s) {
        float const t0 = -kExtent + kSegment * static_cast<float>(s);
        float const t1 = t0 + kSegment;
        debug.line(
          vec3(a, GroundHeight(a, t0), t0), vec3(a, GroundHeight(a, t1), t1),
          kGroundColor, kGroundThickness, Depth::On
        );
        debug.line(
          vec3(t0, GroundHeight(t0, a), a), vec3(t1, GroundHeight(t1, a), a),
          kGroundColor, kGroundThickness, Depth::On
        );
      }
    }
  }

  /* Bones and joint markers, drawn over the mesh (no depth test). */
  void drawSkeleton(DebugDraw& debug) {
    using namespace scene::component;
    auto& reg = renderer_.scene().registry;

    reg.view<Skin, Animator>().each([&](Skin const& skin, Animator const&) {
      if (!skin.skeleton) {
        return;
      }
      auto const& parents = skin.skeleton->parents;
      size_t const count = std::min(skin.joints.size(), parents.size());

      std::vector<bool> has_child(count, false);
      for (size_t j = 0; j < count; ++j) {
        if (parents[j] < count) {
          has_child[parents[j]] = true;
        }
      }
      auto color = [&](size_t j) { return has_child[j] ? kNodeColor : kLeafColor; };

      auto position = [&](size_t j, vec3& out) {
        auto const joint = skin.joints[j];
        if (joint == entt::null || !reg.valid(joint)) {
          return false;
        }
        auto const* global = reg.try_get<GlobalTransform>(joint);
        if (!global) {
          return false;
        }
        out = lina::to_vec3(global->worldMatrix.w);
        return true;
      };

      for (size_t j = 0; j < count; ++j) {
        vec3 p{};
        if (!position(j, p)) {
          continue;
        }

        vec3 q{};
        if (parents[j] < count && position(parents[j], q)) {
          debug.line(q, p, color(j), bone_thickness_, Depth::Off);
        }
        if (show_markers_) {
          debug.cross(p, marker_size_, color(j), joint_thickness_);
        }
      }
    });
  }

  void drawIKTargets(DebugDraw& debug) {
    auto& animation = renderer_.animation();

    for (auto const& c : characters_) {
      for (auto const& entry : c.ik) {
        drawCross(
          debug,
          (c.foot_ik && entry.foot) ? animation.ikTarget(c.root, entry.id) : entry.target,
          kTargetColor
        );
        if (entry.use_pole) {
          drawCross(debug, entry.pole, kPoleColor);
        }
      }
    }
  }

  void drawChainTargets(DebugDraw& debug) {
    auto& animation = renderer_.animation();

    for (auto const& [root, ui] : chains_) {
      if (!ui.id.valid()) {
        continue;
      }
      drawCross(debug, animation.chainIKTarget(root, ui.id), kTargetColor);
      if (ui.use_pole) {
        drawCross(debug, ui.pole, kPoleColor);
      }
    }
  }

  /* The target, and the aim axis of the head (which should point out of the face). */
  void drawLookTargets(DebugDraw& debug) {
    auto& animation = renderer_.animation();

    for (auto const& c : characters_) {
      for (auto const& entry : c.look) {
        drawCross(debug, entry.target, kLookColor);
        if (auto const aim = animation.lookAim(c.root, entry.id)) {
          debug.line(
            aim->origin, aim->origin + aim->direction * 0.4f,
            kLookColor, kTargetThickness, Depth::Off
          );
        }
      }
    }
  }

 private:
  ArcBallController arcball_controller_{};

  AssetHandle asset_{};
  std::shared_ptr<scene::AnimationGraphDesc const> graph_{};
  std::vector<Character> characters_{};
  std::vector<std::string> clip_names_{};
  std::vector<std::string> joint_names_{};
  int current_{0};

  // Two-bone IK
  int   ik_joint_{0};
  float ik_fade_{0.2f};
  bool  ik_free_on_kick_{false};

  // Chain IK (one per instance)
  std::map<entt::entity, ChainUI> chains_{};

  // Foot IK
  bool  show_ground_{true};
  bool  foot_dirty_{false};
  float foot_probe_{0.5f};
  float foot_blend_{0.15f};
  float foot_smooth_{12.0f};
  bool  foot_align_{true};
  float foot_max_angle_{35.0f};

  // Look-at
  int   look_joint_{5};
  int   look_length_{3};
  int   look_axis_{3};
  float look_max_angle_{70.0f};
  float look_fade_angle_{40.0f};
  float look_smooth_{10.0f};
  bool  look_orbit_{false};

  // Display
  bool  show_skeleton_{false};
  bool  show_markers_{true};
  float bone_thickness_{2.0f};
  float joint_thickness_{2.0f};
  float marker_size_{0.03f};
  bool  use_dqs_{false};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */
