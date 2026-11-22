#ifndef AER_SCENE_ECS_ANIMATION_SYSTEM_H_
#define AER_SCENE_ECS_ANIMATION_SYSTEM_H_

#include <optional>
#include <string_view>
#include <string>
#include <vector>

#include "entt/entt.hpp"

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

namespace scene {

class Hierarchy;

//
//    AnimationSystem
//
//  Plays skeletal animations of a Hierarchy and builds the skin palettes.
//
//  A frame is:
//      preUpdate(elapsed_time)     animators write their pose into the joints' Transform
//      Hierarchy::update()         global transforms
//      postUpdate()                skin palettes from the new global transforms
//
class AnimationSystem {
 public:
  static constexpr float kMaxDeltaTime = 0.1f;

 public:
  struct State {
    std::string_view clip{};
    float time{};
    float duration{};
    float speed{1.0f};
    bool loop{true};
    bool playing{false};
  };

 public:
  void setup(Hierarchy& scene) {
    scene_ = &scene;
  }

  // --- Frame ---

  void preUpdate(float elapsed_time);

  void postUpdate();

  void advance(float dt);

  // --- Controls ---

  /* Play `clip_name` from its start on every skeleton of the instance having it.
   * Returns false if none has it. */
  bool play(entt::entity instance_root, std::string_view clip_name, bool loop = true);

  /* Freeze, keeping clip, time and the current pose. */
  void pause(entt::entity instance_root);

  /* Continue a paused clip (or restart a finished non looping clip)
   * Does nothing after stop(). */
  void resume(entt::entity instance_root);

  /* Discard the clip, rewind and put the joints back to their bind pose. */
  void stop(entt::entity instance_root);

  // -----------
  /* Playback speed multiplier (>= 0) of every animator of the instance. */
  void setSpeed(entt::entity instance_root, float speed);

  /* Loop flag of every animator of the instance (applies to the current clip). */
  void setLoop(entt::entity instance_root, bool loop);

  /* Names of the clips of an instance. */
  [[nodiscard]]
  std::vector<std::string> clipNames(entt::entity instance_root) const;

  [[nodiscard]]
  State state(entt::entity instance_root) const;
  // -----------

 private:
  Hierarchy* scene_{};
  std::optional<float> last_elapsed_time_{};
};

} // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ECS_ANIMATION_SYSTEM_H_
