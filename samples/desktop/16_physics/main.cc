/* -------------------------------------------------------------------------- */
//
//    16 - physics
//
//  Where crates fall on the floor.
//
//  Rigid bodies are simulated by the framework and drawn as meshes,
//  with the physics debug overlay on top.
//
//  - Space : shoot a ball from the camera.
//
/* -------------------------------------------------------------------------- */

#include <algorithm>
#include <array>
#include <map>
#include <random>
#include <type_traits>
#include <variant>
#include <vector>

#include "aer/application.h"
#include "aer/core/arcball_controller.h"

#include "aer/scene/asset.h"
#include "aer/scene/ecs/physics_components.h"

/* -------------------------------------------------------------------------- */

class SampleApp final : public Application {
 private:
  static constexpr size_t kMaxBodies = 400u;
  static constexpr float  kKillHeight = -20.0f;
  static constexpr float  kFloorHalfSize = 12.0f;

  static
  scene::MaterialProxy MakeLook(float r, float g, float b, float roughness) {
    scene::MaterialProxy m{};
    m.pbr_mr.basecolor_factor = vec4(r, g, b, 1.0f);
    m.pbr_mr.metallic_factor  = 0.0f;
    m.pbr_mr.roughness_factor = roughness;
    return m;
  }

  static
  scene::MaterialProxy CrateLook() {
    return MakeLook(
      lina::to_linear_channel(0.50f),
      lina::to_linear_channel(0.32f),
      lina::to_linear_channel(0.13f),
      0.7f
    );
  }

  static
  scene::MaterialProxy BallLook() {
    return MakeLook(
      lina::to_linear_channel(0.85f),
      lina::to_linear_channel(0.16f),
      lina::to_linear_channel(0.22f),
      1.0f
    );
  }

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

    renderer_.set_clear_color({ 0.35f, 0.5f, 0.2f, 1.0f });
    renderer_.setSkybox(ASSETS_DIR "textures/qwantani_dusk_2_puresky_2k.hdr");

    /* Bodies are drawn as meshes, the physics debug overlay stays on top. */
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
    if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
      shoot();
    }
    removeFallenBodies();
  }

 private:
  /* Identifies a cached shape asset: its kind, dimensions and look. */
  struct ShapeKey {
    uint32_t kind{};
    std::array<float, 3> dims{};
    std::array<float, 6> look{};   // basecolor rgba, metallic, roughness
    auto operator<=>(ShapeKey const&) const = default;
  };

  static
  ShapeKey KeyOf(physics::ShapeDesc const& shape, scene::MaterialProxy const& material) {
    auto const& pbr = material.pbr_mr;
    std::array<float, 6> const look{
      pbr.basecolor_factor.x, pbr.basecolor_factor.y,
      pbr.basecolor_factor.z, pbr.basecolor_factor.w,
      pbr.metallic_factor, pbr.roughness_factor,
    };
    return std::visit([&](auto const& s) -> ShapeKey {
      using T = std::decay_t<decltype(s)>;
      if constexpr (std::is_same_v<T, physics::BoxShape>) {
        return { 0u, { s.half_extents.x, s.half_extents.y, s.half_extents.z }, look };
      } else if constexpr (std::is_same_v<T, physics::SphereShape>) {
        return { 1u, { s.radius, 0.0f, 0.0f }, look };
      } else {
        return { 2u, { s.half_height, s.radius, 0.0f }, look };
      }
    }, shape);
  }

  /* Visual mesh at the physics size of the shape. */
  static
  scene::Mesh BuildShapeMesh(physics::ShapeDesc const& shape) {
    scene::Mesh mesh{};
    std::visit([&](auto const& s) {
      using T = std::decay_t<decltype(s)>;
      if constexpr (std::is_same_v<T, physics::BoxShape>) {
        Geometry::MakeBox(mesh, s.half_extents.x, s.half_extents.y, s.half_extents.z);
      } else if constexpr (std::is_same_v<T, physics::SphereShape>) {
        Geometry::MakeSphere(mesh, s.radius);
      } else {
        Geometry::MakeCapsule(mesh, s.half_height, s.radius);
      }
    }, shape);
    return mesh;
  }

  /* Instance of the (cached) visual of a shape, or entt::null on failure. */
  entt::entity instantiateShape(
    physics::ShapeDesc const& shape,
    scene::MaterialProxy const& material
  ) {
    auto const key = KeyOf(shape, material);

    auto it = shape_assets_.find(key);
    if (it == shape_assets_.end()) {
      auto const ah = renderer_.gpu_pool().uploadAsset(
        scene::CreateNamedMeshAsset("shape", BuildShapeMesh(shape), material)
      );
      if (!ah.valid()) {
        LOGW("instantiateShape: invalid asset handle");
        return entt::null;
      }
      it = shape_assets_.emplace(key, ah).first;
    }
    return renderer_.createInstance(it->second);
  }

  /* A static body: a Collider on an instance root, no RigidBody. */
  void createStatic(
    physics::ShapeDesc const& shape,
    vec3 const& position,
    quat const& rotation,
    float friction,
    scene::MaterialProxy const& material
  ) {
    using namespace scene::component;

    auto const e = instantiateShape(shape, material);
    if (e == entt::null) {
      return;
    }

    auto& scene = renderer_.scene();
    scene.registry.emplace<Collider>(e, Collider{
      .shape = shape,
      .friction = friction,
    });

    auto& transform = scene.get<Transform>(e);
    transform.translation = position;
    transform.rotation = rotation;
  }

  void createStaticScene() {
    // Floor
    createStatic(
      physics::BoxShape{vec3(kFloorHalfSize, 0.5f, kFloorHalfSize)},
      vec3(0.0f, -0.5f, 0.0f),
      quat{lina::identity},
      0.6f,
      MakeLook(0.30f, 0.33f, 0.37f, 0.9f)
    );
    // Ramp
    createStatic(
      physics::BoxShape{vec3(2.0f, 0.1f, 1.5f)},
      vec3(4.0f, 0.6f, 0.0f),
      lina::rotation_quat(vec3(0.0f, 0.0f, 1.0f), lina::radians(20.0f)),
      0.4f,
      MakeLook(0.85f, 0.62f, 0.15f, 0.5f)
    );
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
      BallLook(),
      3000.0f
    );
  }

  /* Dynamic bodies: a Collider and a RigidBody on the instance root. */
  void spawn(
    physics::ShapeDesc const& shape,
    vec3 const& position,
    quat const& rotation = quat{lina::identity},
    vec3 const& velocity = vec3{},
    scene::MaterialProxy look = CrateLook(),
    float density = 1000.0f
  ) {
    using namespace scene::component;

    if (bodies_.size() >= kMaxBodies) {
      return;
    }

    auto const e = instantiateShape(shape, look);
    if (e == entt::null) {
      return;
    }

    auto& scene = renderer_.scene();
    scene.registry.emplace<Collider>(e, Collider{
      .shape = shape,
      .friction = friction_,
      .restitution = restitution_,
    });
    scene.registry.emplace<RigidBody>(e, RigidBody{
      .density = density,
      .linear_velocity = velocity,
    });

    auto& transform = scene.get<Transform>(e);
    transform.translation = position;
    transform.rotation = rotation;

    bodies_.push_back(e);
  }

  void clearBodies() {
    for (auto const e : bodies_) {
      renderer_.destroyInstance(e);
    }
    bodies_.clear();
    stacks_ = 0;
  }

  /* Destroying the instance destroys its body. */
  void removeFallenBodies() {
    using namespace scene::component;
    auto& scene = renderer_.scene();

    std::erase_if(bodies_, [&](entt::entity e) {
      if (scene.get<Transform>(e).translation.y >= kKillHeight) {
        return false;
      }
      renderer_.destroyInstance(e);
      return true;
    });
  }

  float random(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng_);
  }

 private:
  ArcBallController arcball_controller_{};

   // Cache of mesh assets.
  std::map<ShapeKey, AssetHandle> shape_assets_{};

  std::vector<entt::entity> bodies_{};
  int stacks_{0};

  float crate_size_{0.5f};
  float friction_{0.6f};
  float restitution_{0.0f};
  int stack_columns_{4};
  int stack_rows_{5};
  float shoot_speed_{18.0f};

  std::mt19937 rng_{1234u};
};

// ----------------------------------------------------------------------------

ENTRY_POINT(SampleApp)

/* -------------------------------------------------------------------------- */