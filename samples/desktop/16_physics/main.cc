/* -------------------------------------------------------------------------- */
//
//    16 - physics
//
//  Where crates fall on the floor.
//
//  Rigid bodies are simulated by the framework (Jolt) and shown through the
//  physics debug drawing.
//
//  - Space : shoot a ball from the camera.
//
/* -------------------------------------------------------------------------- */

#include <algorithm>
#include <random>
#include <vector>

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "aer/scene/ecs/physics_components.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  static constexpr size_t kMaxBodies = 400u;
  static constexpr float  kKillHeight = -20.0f;
  static constexpr float  kFloorHalfSize = 12.0f;

 private:
  bool setup() final {
    wm_->set_title("16 - physics");

    /* Setup the ArcBall camera. */
    {
      arcball_controller_.set_target(vec3(0.0f, 1.0f, 0.0f));
      arcball_controller_.set_view(lina::kPi/16.0f, lina::kPi/6.0f);
      arcball_controller_.set_dolly(12.0f);
      camera_.set_controller(&arcball_controller_);
    }

    renderer_.set_clear_color({ 0.02f, 0.02f, 0.01f, 1.0f });
    renderer_.setSkybox(ASSETS_DIR "textures/qwantani_dusk_2_puresky_2k.hdr");

    /* Bodies are drawn by the physics debug rendering. */
    renderer_.physics().debug().enabled = true;

    createStaticScene();
    spawnStack();

    return true;
  }

  void buildUI() final {
    auto& physics = renderer_.physics();
    auto& debug = physics.debug();

    ImGui::Begin("Settings");
    {
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::Text("Bodies: %u", physics.world().body_count());

      bool paused = physics.paused();
      if (ImGui::Checkbox("Pause", &paused)) {
        physics.set_paused(paused);
      }

      ImGui::SeparatorText("Spawn");
      if (ImGui::Button("Crate")) {
        dropShape(physics::BoxShape{vec3(0.5f * crate_size_)});
      }
      ImGui::SameLine();
      if (ImGui::Button("Sphere")) {
        dropShape(physics::SphereShape{0.5f * crate_size_});
      }
      ImGui::SameLine();
      if (ImGui::Button("Capsule")) {
        dropShape(physics::CapsuleShape{0.4f * crate_size_, 0.25f * crate_size_});
      }

      ImGui::SliderFloat("Size (m)", &crate_size_, 0.2f, 1.5f, "%.2f");
      ImGui::SliderFloat("Friction", &friction_, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Restitution", &restitution_, 0.0f, 1.0f, "%.2f");

      ImGui::SeparatorText("Stack");
      ImGui::SliderInt("Columns", &stack_columns_, 1, 10);
      ImGui::SliderInt("Rows", &stack_rows_, 1, 10);
      if (ImGui::Button("Add stack")) {
        spawnStack();
      }
      ImGui::SameLine();
      if (ImGui::Button("Reset")) {
        clearBodies();
        spawnStack();
      }

      ImGui::SeparatorText("Shoot (Space)");
      ImGui::SliderFloat("Speed (m/s)", &shoot_speed_, 2.0f, 40.0f, "%.1f");
      if (ImGui::Button("Shoot")) {
        shoot();
      }

      ImGui::SeparatorText("Debug draw");
      ImGui::Checkbox("Enabled", &debug.enabled);
      ImGui::Checkbox("Shapes", &debug.shapes);
      ImGui::Checkbox("Sleeping colors", &debug.sleeping_colors);
      ImGui::Checkbox("Bounding boxes", &debug.bounding_boxes);
      ImGui::Checkbox("Centers of mass", &debug.centers_of_mass);
      ImGui::Checkbox("Velocities", &debug.velocities);
      ImGui::Checkbox("Depth test", &debug.depth_test);
    }
    ImGui::End();
  }

  void update(float const) final {
    auto const& io = ImGui::GetIO();
    if (!io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      shoot();
    }
    removeFallenBodies();
  }

 private:
  // [wip]
  entt::entity createBox(float size) {
    scene::Asset asset{};

    asset.meshes.push_back({});
    auto &mesh = asset.meshes[0];
    Geometry::MakeCube(mesh, size);

    auto &pool = renderer_.gpu_pool();
    auto ah = pool.uploadAsset(std::move(asset)); //

    if (!ah.valid()) {
      LOGW("createBox: invalid asset handle");
      return entt::null;
    }

    return renderer_.createInstance(ah);
  }

  /* Floor and a ramp, both static (a Collider alone). */
  void createStaticScene() {
    using namespace scene::component;
    auto& scene = renderer_.scene();

    auto const floor = scene.createStagingEntity<Collider>(entt::null);
    scene.get<Collider>(floor) = {
      .shape = physics::BoxShape{vec3(kFloorHalfSize, 0.5f, kFloorHalfSize)},
      .friction = 0.6f,
    };
    scene.get<Transform>(floor).translation = vec3(0.0f, -0.5f, 0.0f);

    auto const ramp = scene.createStagingEntity<Collider>(entt::null);
    scene.get<Collider>(ramp) = {
      .shape = physics::BoxShape{vec3(2.0f, 0.1f, 1.5f)},
      .friction = 0.4f,
    };
    auto& ramp_transform = scene.get<Transform>(ramp);
    ramp_transform.translation = vec3(4.0f, 0.6f, 0.0f);
    ramp_transform.rotation = lina::rotation_quat(vec3(0.0f, 0.0f, 1.0f), lina::radians(20.0f));
  }

  /* A brick wall of crates, in front of the camera. */
  void spawnStack() {
    float const size = crate_size_;
    float const half = 0.5f * size;
    float const z = -2.0f + 1.5f * size * static_cast<float>(stacks_++ % 3);

    for (int row = 0; row < stack_rows_; ++row) {
      float const shift = (row % 2) ? half : 0.0f;
      for (int col = 0; col < stack_columns_; ++col) {
        float const x = (static_cast<float>(col) - 0.5f * static_cast<float>(stack_columns_ - 1)) * size
                      + shift - 3.0f;
        float const y = half + size * static_cast<float>(row);
        spawn(physics::BoxShape{vec3(half)}, vec3(x, y, z));
      }
    }
  }

  void dropShape(physics::ShapeDesc const& shape) {
    vec3 const position(random(-1.5f, 1.5f), random(5.0f, 7.0f), random(-1.5f, 1.5f));
    vec3 const axis = lina::normalize(vec3(random(-1.0f, 1.0f), random(0.1f, 1.0f), random(-1.0f, 1.0f)));
    quat const rotation = lina::rotation_quat(axis, random(0.0f, lina::kPi));
    spawn(shape, position, rotation);
  }

  void shoot() {
    auto const& view = camera_.transform();
    vec3 const direction = view.direction();

    spawn(
      physics::SphereShape{0.2f},
      view.position() + 1.0f * direction,
      quat{lina::identity},
      shoot_speed_ * direction,
      3000.0f
    );
  }

  void spawn(
    physics::ShapeDesc const& shape,
    vec3 const& position,
    quat const& rotation = quat{lina::identity},
    vec3 const& velocity = vec3{},
    float density = 1000.0f
  ) {
    using namespace scene::component;

    if (bodies_.size() >= kMaxBodies) {
      return;
    }

    auto& scene = renderer_.scene();

#if 1
    auto const e = scene.createStagingEntity<Collider, RigidBody>(entt::null);

    scene.get<Collider>(e) = {
      .shape = shape,
      .friction = friction_,
      .restitution = restitution_,
    };
    scene.get<RigidBody>(e) = {
      .density = density,
      .linear_velocity = velocity,
    };
#else
    auto& reg = scene.registry;

    auto const e = createBox(1.0f);

    reg.emplace<Collider>(e, Collider{
      .shape = shape,
      .friction = friction_,
      .restitution = restitution_,
    });
    reg.emplace<RigidBody>(e, RigidBody{
      .density = density,
      .linear_velocity = velocity,
    });
#endif
    auto& transform = scene.get<Transform>(e);
    transform.translation = position;
    transform.rotation = rotation;

    bodies_.push_back(e);
  }

  void clearBodies() {
    auto& scene = renderer_.scene();
    for (auto const e : bodies_) {
      scene.destroySubtree(e);
    }
    bodies_.clear();
    stacks_ = 0;
  }

  /* Destroying the entity destroys its body. */
  void removeFallenBodies() {
    using namespace scene::component;
    auto& scene = renderer_.scene();

    std::erase_if(bodies_, [&](entt::entity e) {
      if (scene.get<Transform>(e).translation.y >= kKillHeight) {
        return false;
      }
      scene.destroySubtree(e);
      return true;
    });
  }

  float random(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng_);
  }

 private:
  ArcBallController arcball_controller_{};

  std::vector<entt::entity> bodies_{};
  std::mt19937 rng_{1234u};
  int stacks_{0};

  float crate_size_{0.5f};
  float friction_{0.6f};
  float restitution_{0.0f};
  int stack_columns_{4};
  int stack_rows_{5};
  float shoot_speed_{18.0f};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */