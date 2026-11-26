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
#include "aer/scene/animation.h"
#include "aer/scene/animation_ik.h"
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

  // --------------------------------
  struct TwoBoneIKDesc {
    std::string_view root{};
    std::string_view mid{};
    std::string_view end{};
    float weight{1.0f};
  };

  struct ChainLimitDesc {
    float max_angle{180.0f};  // degrees from the reference direction (180 = free)
    bool hinge{false};        // bend only about the animated bend axis, never backward
  };

  struct ChainIKDesc {
    std::string_view end{};
    uint32_t length{3};       // bones from `end` toward the root (>= 2)
    float weight{1.0f};
    FABRIKParams params{};
    std::vector<ChainLimitDesc> limits{};   // per bone from the root, missing = free
  };

  struct FootIKDesc {
    std::vector<IKId> feet{};
    std::string_view pelvis{};
    FootRig::Params params{
      // ankle_height: when negative will measure it from the current pose
      // max_angle: in degrees
    };
  };

  struct LookAtDesc {
    std::string_view head{};
    uint32_t length{3};       // ancestors sharing the rotation
    LookAt::Params params{
      // max_angle, fade_angle in degrees
    };
  };

  struct LookAtAim {
    vec3 origin{};
    vec3 direction{};
  };
  // --------------------------------

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

  /* Joint names of an instance (skeleton order). */
  [[nodiscard]]
  std::vector<std::string> jointNames(entt::entity instance_root) const;

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

  // -------------------------------
  // --- Two Bones IK ---

  /* Add a two-bone chain to every animator of the instance having its joints.
   * Chains are solved after the hierarchy update on top of the active motion,
   * a stopped animator has no IK.
   * Returns an invalid id if no animator has the chain. */
  [[nodiscard]]
  IKId addTwoBoneIK(entt::entity instance_root, TwoBoneIKDesc const& desc);

  bool removeIK(entt::entity instance_root, IKId id);

  /* World space target of the end joint. */
  bool setIKTarget(entt::entity instance_root, IKId id, vec3 const& world_position);

  /* [0, 1] blend between the animated pose and the IK solution.
   * [TODO] add a start_time for non linear blending.  */
  bool setIKWeight(entt::entity instance_root, IKId id, float weight, float blend_seconds = 0.0f);

  /* Make the bend plane face a world space position. */
  bool setIKPole(entt::entity instance_root, IKId id, vec3 const& world_position);

  bool clearIKPole(entt::entity instance_root, IKId id);

  [[nodiscard]]
  float ikWeight(entt::entity instance_root, IKId id) const;

  [[nodiscard]]
  vec3 ikPole(entt::entity instance_root, IKId id) const;

  [[nodiscard]]
  vec3 ikTarget(entt::entity instance_root, IKId id) const;

  [[nodiscard]]
  std::vector<std::string> twoBoneIKEndJoints(entt::entity instance_root) const;

  // -------------------------------
  // --- Chain IK (FABRIK) ---

  /* N-bone chain ending at `desc.end` and going up `desc.length` bones. The root joint stays
   * in place, the others rotate so the end reaches the target (a target out of reach
   * straightens the chain, bones are never stretched). The target starts on the current end. */
  [[nodiscard]]
  ChainIKId addChainIK(entt::entity instance_root, ChainIKDesc const& desc);

  bool removeChainIK(entt::entity instance_root, ChainIKId id);

  bool setChainIKTarget(entt::entity instance_root, ChainIKId id, vec3 const& world_position);

  /* Same fade rules as setIKWeight. */
  bool setChainIKWeight(entt::entity instance_root, ChainIKId id, float weight, float blend_seconds = 0.0f);

  /* Twist the inner joints about the root -> end axis so they face a world space position. */
  bool setChainIKPole(entt::entity instance_root, ChainIKId id, vec3 const& world_position);

  bool clearChainIKPole(entt::entity instance_root, ChainIKId id);

  /* Replace the limits of a chain (same layout as ChainIKDesc::limits). */
  bool setChainIKLimits(
    entt::entity instance_root,
    ChainIKId id,
    std::vector<ChainLimitDesc> const& limits
  );

  [[nodiscard]]
  vec3 chainIKTarget(entt::entity instance_root, ChainIKId id) const;

  [[nodiscard]]
  float chainIKWeight(entt::entity instance_root, ChainIKId id) const;

  // -------------------------------
  // --- Foot IK ---

  /* Ground height provider shared by every foot rig. */
  void setGroundQuery(GroundQuery query);

  /* Place the feet of the instance on the ground. */
  bool setFootIK(entt::entity instance_root, FootIKDesc const& desc);

  void clearFootIK(entt::entity instance_root);

  [[nodiscard]]
  float footIKAnkleHeight(entt::entity instance_root) const;

  // -------------------------------
  // --- Look-at ---

  /* Added to every animator of the instance having the joints. The target starts straight ahead
   * of the head, so nothing moves. Needs an active motion, like IK. */
  [[nodiscard]]
  LookId addLookAt(entt::entity instance_root, LookAtDesc const& desc);

  bool removeLookAt(entt::entity instance_root, LookId id);

  bool setLookTarget(entt::entity instance_root, LookId id, vec3 const& world_position);

  /* Same fade rules as setIKWeight. */
  bool setLookWeight(entt::entity instance_root, LookId id, float weight, float blend_seconds = 0.0f);

  [[nodiscard]]
  vec3 lookTarget(entt::entity instance_root, LookId id) const;

  /* Head position and where its aim axis points (for debug drawing). */
  [[nodiscard]]
  std::optional<LookAtAim> lookAim(entt::entity instance_root, LookId id) const;

  // -------------------------------
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
  PoseCache poses_{};
  std::map<GraphKey, std::weak_ptr<AnimationGraph const>> graph_cache_{};

  std::optional<float> last_elapsed_time_{};
  float last_dt_{0.0f};

  GroundQuery ground_query_{};

  uint32_t next_ik_id_{0u};
  uint32_t next_chain_id_{0u};
  uint32_t next_look_id_{0u};
};

} // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ECS_ANIMATION_SYSTEM_H_
