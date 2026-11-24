#ifndef AER_SCENE_ECS_ANIMATION_SYSTEM_H_
#define AER_SCENE_ECS_ANIMATION_SYSTEM_H_

#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

#include "entt/entt.hpp"

#include "aer/core/common.h"
#include "aer/scene/animation.h" // for BlendTree1D
#include "aer/scene/animation_pose_cache.h"

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
//      Hierarchy::update()         compute global transforms
//      postUpdate()                compute skin palettes from the new global transforms
//
class AnimationSystem {
 public:
  static constexpr float kMaxDeltaTime = 0.1f;

 public:
  struct State {
    std::string_view clip{};  // animation or tree name
    std::string_view machine_state{};
    float time{};
    float duration{};
    float speed{1.0f};
    float fade{1.0f};
    bool loop{true};
    bool playing{false};
  };

 public:
  void setup(Hierarchy& scene) {
    scene_ = &scene;
  }

  // -------------------------------
  // --- Frame ---

  void preUpdate(float elapsed_time);

  void postUpdate();

  void advance(float dt);

  // -------------------------------
  // --- Controls ---

  /* Play `clip_name` from its start on every skeleton of the instance having it.
   * Bypass any binded animation state machine.
   * Returns false if none has it. */
  bool play(
    entt::entity instance_root,
    std::string_view clip_name,
    bool loop = true,
    float fade_duration = 0.0f
  );

  /* Freeze, keeping clip, time and the current pose. */
  void pause(entt::entity instance_root);

  /* Continue a paused clip (or restart a finished non looping clip). */
  void resume(entt::entity instance_root);

  /* Discard the clip, rewind and put the joints back to their bind pose. */
  void stop(entt::entity instance_root);

  /* Jump to `time` and resample. */
  void setTime(entt::entity instance_root, float time);

  /* Playback speed multiplier of every animator of the instance. */
  void setSpeed(entt::entity instance_root, float speed);

  /* Loop flag of every animator of the instance (applies to the current clip). */
  void setLoop(entt::entity instance_root, bool loop);

  /* Names of the clips of an instance. */
  [[nodiscard]]
  std::vector<std::string> clipNames(entt::entity instance_root) const;

  /* Information of the current animation being played. */
  [[nodiscard]]
  State state(entt::entity instance_root) const;

  // -------------------------------
  // --- Animation Graph ---

  /* Assign a graph to every animator of the instance and enter its initial state.
   * The description is compiled once per skeleton and shared by every instance given the same
   * pointer. Parameters are reset to their defaults and a the current motion stops.
   * Returns false if no animator could resolve it. */
  bool setGraph(entt::entity instance_root, std::shared_ptr<AnimationGraphDesc const> graph);

  /* Play `tree_name` of the graph on every animator having it.
   * Bypass any binded animation state machine.
   * Returns false if none has it. */
  bool playBlendTree(
    entt::entity instance_root,
    std::string_view tree_name,
    float fade_duration = 0.0f
  );

  // -------------------------------
  // --- Parameters ---

  /* Handles are the same for every instance using the same graph. */
  [[nodiscard]]
  ParamId findParameter(entt::entity instance_root, std::string_view name) const;

  bool setParameter(entt::entity instance_root, ParamId id, float value);
  bool setParameter(entt::entity instance_root, std::string_view name, float value);

  [[nodiscard]]
  float parameter(entt::entity instance_root, ParamId id) const;

  [[nodiscard]]
  float parameter(entt::entity instance_root, std::string_view name) const;

  /* Triggers are one-shot parameters.
   * A pending trigger stays so until a transition having it as a condition fires, which consumes it, or until its expiry lapses.
   * `pause` keeps it, `setGraph` clears it.
   * Return false if the instance has no such trigger. */
  bool setTrigger(entt::entity instance_root, ParamId id);
  bool setTrigger(entt::entity instance_root, std::string_view name);

  /* Drop a pending trigger without consuming it. */
  bool resetTrigger(entt::entity instance_root, ParamId id);
  bool resetTrigger(entt::entity instance_root, std::string_view name);

  // -------------------------------
  // --- States ---

  [[nodiscard]]
  StateId findState(entt::entity instance_root, std::string_view name) const;

  /* Force a transition to a state of the machine. */
  bool setState(entt::entity instance_root, StateId state, float fade_duration = 0.0f);
  bool setState(entt::entity instance_root, std::string_view state_name, float fade_duration = 0.0f);

  /* Names of the AnimationStateMachine states of an instance. */
  [[nodiscard]]
  std::vector<std::string> stateNames(entt::entity instance_root) const;

 private:
  /* Compiled graph for (desc, skeleton), shared through the cache. */
  [[nodiscard]]
  std::shared_ptr<AnimationGraph const> acquireGraph(
    std::shared_ptr<AnimationGraphDesc const> const& desc,
    Skeleton const& skeleton
  );

 private:
  using GraphKey = std::pair<AnimationGraphDesc const*, Skeleton const*>;

  Hierarchy* scene_{};
  std::optional<float> last_elapsed_time_{};
  PoseCache poses_{};
  std::map<GraphKey, std::weak_ptr<AnimationGraph const>> graph_cache_{};
};

} // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ECS_ANIMATION_SYSTEM_H_
