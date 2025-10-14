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

  bool mouseMoved() const noexcept { return mouse_moved_; }
  bool hasButtonDown() const noexcept { return mouse_button_down_; }
  bool hasResized() const noexcept { return has_resized_; }

  uint32_t surfaceWidth() const noexcept { return surface_w_; };
  uint32_t surfaceHeight() const noexcept { return surface_h_; };

  int mouseX() const noexcept { return mouse_x_; }
  int mouseY() const noexcept { return mouse_y_; }

  float wheel() const noexcept { return mouse_wheel_; }
  float wheelDelta() const noexcept { return mouse_wheel_delta_; }

  bool buttonDown(KeyCode_t button) const noexcept;
  bool buttonPressed(KeyCode_t button) const noexcept;
  bool buttonReleased(KeyCode_t button) const noexcept;

  bool keyDown(KeyCode_t key) const noexcept;
  bool keyPressed(KeyCode_t key) const noexcept;
  bool keyReleased(KeyCode_t key) const noexcept;

  /* Return the last user's keystroke. */
  KeyCode_t lastKeyDown() const noexcept {
    return key_pressed_.empty() ? -1 : key_pressed_.top(); //
  }

  /* Return the last frame input char, if any. */
  uint16_t lastInputChar() const noexcept {
    return last_input_char_;
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

 private:
  Events() = default;
  ~Events() final = default;

  using KeyMap_t = std::unordered_map<KeyCode_t, KeyState>;
  using StatePredicate_t = std::function<bool(KeyState)> const&;

  /* Check if a key state match a state predicate. */
  bool checkButtonState(KeyCode_t button, StatePredicate_t predicate) const noexcept;
  bool checkKeyState(KeyCode_t key, StatePredicate_t predicate) const noexcept;

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

  // Registered events callbacks.
  std::set<EventCallbacksPtr> event_callbacks_{};

 private:
  friend class Singleton<Events>;
};

// ----------------------------------------------------------------------------

#endif // AER_CORE_EVENTS_H_
