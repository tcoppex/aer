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

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  struct Character {
    entt::entity root{entt::null};
    int   clip{0};       // index in clip_names_
    float speed{1.0f};
    bool  loop{true};
  };

  struct LineVertex {
    vec3 position;
    vec3 color;
  };

  struct PushConstant {
    mat4 view_projection;
  };

  static constexpr uint32_t kMaxLineVertices = 1u << 15u;

  static constexpr vec3 kNodeColor{1.0f, 0.70f, 0.15f};
  static constexpr vec3 kLeafColor{0.20f, 0.90f, 0.40f};

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

    renderer_.set_clear_color({ 0.02f, 0.02f, 0.01f, 1.0f });
    renderer_.setSkybox(ASSETS_DIR "textures/qwantani_dusk_2_puresky_2k.hdr");

    /* Load the asset once, instances share it. */
    asset_ = renderer_.loadAsset(ASSETS_DIR "models/genan_m.glb");
    if (!asset_.valid()) {
      return false;
    }

    addCharacter();
    if (characters_.empty()) {
      return false;
    }

    return createSkeletonPipeline();
  }

  void release() final {
    for (auto const& buffer : line_buffers_) {
      context_.destroyBuffer(buffer);
    }
    context_.destroyResources(pipeline_, pipeline_layout_);
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

        ImGui::Separator();

        ImGui::Checkbox("Skeleton", &show_skeleton_);
        if (show_skeleton_) {
          ImGui::Checkbox("Joint markers", &show_markers_);
          ImGui::SliderFloat("Marker size", &marker_size_, 0.005f, 0.1f, "%.3f");
        }

        if (!state.clip.empty()) {
          const float progress = (state.duration > 0.0f) ? (state.time / state.duration) : 0.0f;
          ImVec2 bar_min = ImGui::GetCursorScreenPos();

          ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f), "");

          ImVec2 bar_max = ImGui::GetItemRectMax();
          char label[64];
          std::snprintf(label, sizeof(label), "%.2f / %.2f s", state.time, state.duration);
          ImVec2 text_size = ImGui::CalcTextSize(label);
          const float pad_x = ImGui::GetStyle().FramePadding.x;
          ImVec2 text_pos(
            bar_min.x + pad_x,
            bar_min.y + (bar_max.y - bar_min.y - text_size.y) * 0.5f
          );
          ImGui::GetWindowDrawList()->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), label);
        }
      }
    }
    ImGui::End();
  }

  void drawPass(RenderPassEncoder const& pass) final {
    if (!show_skeleton_) {
      return;
    }

    line_vertices_.clear();
    buildSkeleton();
    if (line_vertices_.empty()) {
      return;
    }

    auto const& buffer = line_buffers_[renderer_.frame_slot()];
    context_.writeBuffer(
      buffer, 0u,
      line_vertices_.data(), 0u,
      line_vertices_.size() * sizeof(LineVertex)
    );

    PushConstant const pc{
      .view_projection = camera_.transforms()[0].view_projection
    };
    pass.bindPipeline(pipeline_);
    pass.pushConstant(pc, VK_SHADER_STAGE_VERTEX_BIT);
    pass.bindVertexBuffer(buffer);
    pass.draw(static_cast<uint32_t>(line_vertices_.size()), 1u);
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

  bool createSkeletonPipeline() {
    pipeline_layout_ = context_.createPipelineLayout({
      .pushConstantRanges = {
        {
          .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
          .size = sizeof(PushConstant),
        }
      },
    });

    auto shader = context_.createShaderModule(SAMPLE_SPIRV_DIR, "skeleton.slang");

    pipeline_ = context_.createGraphicsPipeline(pipeline_layout_, {
      .vertex = {
        .module = shader.handle,
        .entryPoint = "vertexMain",
        .buffers = {
          {
            .stride = sizeof(LineVertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
            .attributes = {
              { 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, position) },
              { 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, color) },
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
      // (no depth test: the skeleton shows through the mesh)
      .primitive = {
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      },
    });
    context_.releaseShaderModule(shader);

    line_buffers_.resize(renderer_.frames_in_flight());
    for (auto& buffer : line_buffers_) {
      buffer = context_.createBuffer(
        "Skeleton::Lines",
        kMaxLineVertices * sizeof(LineVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_AUTO,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT
      );
      if (!buffer.valid()) {
        return false;
      }
    }
    return true;
  }

  void appendLine(vec3 const& a, vec3 const& color_a, vec3 const& b, vec3 const& color_b) {
    if (line_vertices_.size() + 2u > kMaxLineVertices) {
      return;
    }
    line_vertices_.push_back({ a, color_a });
    line_vertices_.push_back({ b, color_b });
  }

  /* One skeleton per (instance, skeleton): the Animator sits on a single node of each. */
  void buildSkeleton() {
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
          appendLine(q, color(parents[j]), p, color(j));
        }

        if (show_markers_) {
          auto const c = color(j);
          float const s = marker_size_;
          appendLine(p - vec3(s, 0.0f, 0.0f), c, p + vec3(s, 0.0f, 0.0f), c);
          appendLine(p - vec3(0.0f, s, 0.0f), c, p + vec3(0.0f, s, 0.0f), c);
          appendLine(p - vec3(0.0f, 0.0f, s), c, p + vec3(0.0f, 0.0f, s), c);
        }
      }
    });
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

  bool show_skeleton_{false};
  bool show_markers_{true};
  float marker_size_{0.03f};

  VkPipelineLayout pipeline_layout_{};
  Pipeline pipeline_{};
  std::vector<backend::Buffer> line_buffers_{}; // one per frame slot
  std::vector<LineVertex> line_vertices_{};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */