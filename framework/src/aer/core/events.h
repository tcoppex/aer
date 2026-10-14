#ifndef AER_CORE_EVENTS_H_
#define AER_CORE_EVENTS_H_

#include <functional>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "aer/core/singleton.h"
#include "aer/core/event_callbacks.h"

// ----------------------------------------------------------------------------

/**
 * Manage and post-process captured event signals then dispatch them to
 * sub event handlers.
 */
class Events final : public Singleton<Events>
                   , public EventCallbacks
{
 public:
  /* Different states for a key / button :
   *  - "Pressed" and "Released" stay in this state for only one frame.
   *  - "Pressed" is also considered "Down",
   *  - "Released" is also considered "Up".
   */
  enum class KeyState : uint8_t {
    Up,
    Down,
    Pressed,
    Released
  };

  /* Update internal data for the next frame. */
  void prepareNextFrame();

  /* Register event callbacks to dispatch signals to. */
  void registerCallbacks(EventCallbacksPtr eh) {
    event_callbacks_.insert(eh);
  }

  /* Getters */

  [[nodiscard]]
  bool has_mouse_moved() const noexcept { return mouse_moved_; }

  [[nodiscard]]
  bool has_mouse_button_down() const noexcept { return mouse_button_down_; }

  [[nodiscard]]
  bool has_resized() const noexcept { return has_resized_; }

  [[nodiscard]]
  uint32_t surface_width() const noexcept { return surface_w_; };

  [[nodiscard]]
  uint32_t surface_height() const noexcept { return surface_h_; };

  [[nodiscard]]
  int mouse_x() const noexcept { return mouse_x_; }

  [[nodiscard]]
  int mouse_y() const noexcept { return mouse_y_; }

  [[nodiscard]]
  float wheel() const noexcept { return mouse_wheel_; }

  [[nodiscard]]
  float wheel_delta() const noexcept { return mouse_wheel_delta_; }

  [[nodiscard]]
  uint16_t last_input_char() const noexcept { return last_input_char_; }

  // ---

  [[nodiscard]]
  vec2 mouseCoords() const noexcept { return vec2(mouse_x_, mouse_y_); }

  [[nodiscard]]
  bool buttonDown(KeyCode_t button) const noexcept;

  [[nodiscard]]
  bool buttonPressed(KeyCode_t button) const noexcept;

  [[nodiscard]]
  bool buttonReleased(KeyCode_t button) const noexcept;

  [[nodiscard]]
  bool keyDown(KeyCode_t key) const noexcept;

  [[nodiscard]]
  bool keyPressed(KeyCode_t key) const noexcept;

  [[nodiscard]]
  bool keyReleased(KeyCode_t key) const noexcept;

  [[nodiscard]]
  bool gamepadButtonDown(KeyCode_t button) const noexcept;

  [[nodiscard]]
  bool gamepadButtonPressed(KeyCode_t button) const noexcept;

  [[nodiscard]]
  bool gamepadButtonReleased(KeyCode_t button) const noexcept;

  [[nodiscard]]
  float gamepadAxis(int axe_id) const {
    return gamepad_axis_[axe_id];
  }

  /* Return the last user's keystroke. */
  [[nodiscard]]
  KeyCode_t lastKeyDown() const noexcept {
    return key_pressed_.empty() ? -1 : key_pressed_.top(); //
  }

 public:
  /* EventCallbacks override */

  void onKeyPressed(KeyCode_t key) final;

  void onKeyReleased(KeyCode_t key) final;

  void onInputChar(uint16_t c) final;

  void onPointerDown(int x, int y, KeyCode_t button) final;

  void onPointerUp(int x, int y, KeyCode_t button) final;

  void onPointerMove(int x, int y) final;

  void onMouseWheel(float dx, float dy) final;

  void onResize(int w, int h) final;

  //----------------------
  void onGamepadAxisMove(int axe_id, float dx) {
    dx = (fabs(dx) < 0.005f) ? 0.0f : dx;
    gamepad_axis_[axe_id] = dx;
  }
  void onGamepadButtonPressed(int button_id) {
    auto &s = gamepad_buttons_[button_id];
    s = (s == KeyState::Up || s == KeyState::Released) ? KeyState::Pressed
                                                       : KeyState::Down
                                                       ;
  }
  void onGamepadButtonReleased(int button_id) {
    auto &s = gamepad_buttons_[button_id];
    s = (s == KeyState::Down || s == KeyState::Pressed) ? KeyState::Released
                                                        : KeyState::Up
                                                        ;
  }
  //----------------------

 private:
  Events() = default;
  ~Events() final = default;

  using KeyMap_t = std::unordered_map<KeyCode_t, KeyState>;
  using StatePredicate_t = std::function<bool(KeyState)> const&;

  /* Check if a key state match a state predicate. */
  bool checkButtonState(KeyCode_t button, StatePredicate_t predicate) const noexcept;
  bool checkKeyState(KeyCode_t key, StatePredicate_t predicate) const noexcept;
  bool checkGamepadButtonState(KeyCode_t button, StatePredicate_t predicate) const noexcept;

  // Per-frame state.
  bool mouse_moved_{};
  bool mouse_button_down_{};
  bool has_resized_{};

  // Window
  uint32_t surface_w_{};
  uint32_t surface_h_{};

  // Mouse.
  int mouse_x_{};
  int mouse_y_{};
  float mouse_wheel_{};
  float mouse_wheel_delta_{};

  // Buttons.
  KeyMap_t buttons_{};

  // Keys.
  KeyMap_t keys_{};
  std::stack<KeyCode_t> key_pressed_{};

  // Char input.
  uint16_t last_input_char_{};

  // Gamepad (Joystick 1).
  std::array<float, 6> gamepad_axis_{};
  KeyMap_t gamepad_buttons_{};

  // Registered events callbacks.
  std::set<EventCallbacksPtr> event_callbacks_{};

 private:
  friend class Singleton<Events>;
};

// ----------------------------------------------------------------------------

#endif // AER_CORE_EVENTS_H_
