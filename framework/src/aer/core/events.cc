#include "aer/core/events.h"

#include <algorithm>

/* -------------------------------------------------------------------------- */

/* Dispatch event signal to sub callbacks handlers. */
#define EVENTS_DISPATCH_SIGNAL(funcName, ...) \
  static_assert(std::string_view(#funcName)==__func__, "Incorrect dispatch signal used."); \
  std::for_each(event_callbacks_.begin(), event_callbacks_.end(), [__VA_ARGS__](auto &e){ e->funcName(__VA_ARGS__); })

/* -------------------------------------------------------------------------- */

void Events::prepareNextFrame() {
  // Reset per-frame values.
  mouse_moved_        = false;
  has_resized_        = false;
  last_input_char_    = 0;
  mouse_wheel_delta_  = 0.0f;

  // Detect if any mouse buttons are still "Pressed" or "Down".
  mouse_button_down_ = std::any_of(buttons_.cbegin(), buttons_.cend(), [](auto const& btn) {
    auto const state(btn.second);
    return (state == KeyState::Down) || (state == KeyState::Pressed);
  });

  // Update "Pressed" states to "Down", "Released" states to "Up".
  auto const update_state{ [](auto& btn) {
    auto const state = btn.second;
    btn.second = (state == KeyState::Pressed) ? KeyState::Down
               : (state == KeyState::Released) ? KeyState::Up
               : state;
  }};
  std::for_each(buttons_.begin(), buttons_.end(), update_state);
  std::for_each(keys_.begin(), keys_.end(), update_state);
  std::for_each(gamepad_buttons_.begin(), gamepad_buttons_.end(), update_state);
}

/* -------------------------------------------------------------------------- */

void Events::onKeyPressed(KeyCode_t key) {
  keys_[key] = KeyState::Pressed;
  key_pressed_.push( key );

  EVENTS_DISPATCH_SIGNAL(onKeyPressed, key);
}

void Events::onKeyReleased(KeyCode_t key) {
  keys_[key] = KeyState::Released;

  EVENTS_DISPATCH_SIGNAL(onKeyReleased, key);
}

void Events::onInputChar(uint16_t c) {
  last_input_char_ = c;

  EVENTS_DISPATCH_SIGNAL(onInputChar, c);
}

void Events::onPointerDown(int x, int y, KeyCode_t button) {
  buttons_[button] = KeyState::Pressed;

  EVENTS_DISPATCH_SIGNAL(onPointerDown, x, y, button);
}

void Events::onPointerUp(int x, int y, KeyCode_t button) {
  buttons_[button] = KeyState::Released;

  EVENTS_DISPATCH_SIGNAL(onPointerUp, x, y, button);
}

void Events::onPointerMove(int x, int y) {
  mouse_x_ = x;
  mouse_y_ = y;
  mouse_moved_ = true;

  EVENTS_DISPATCH_SIGNAL(onPointerMove, x, y);
}

void Events::onMouseWheel(float dx, float dy) {
  mouse_wheel_delta_ = dy;
  mouse_wheel_ += dy;

  EVENTS_DISPATCH_SIGNAL(onMouseWheel, dx, dy);
}

void Events::onResize(int w, int h) {
  surface_w_ = static_cast<uint32_t>(w);
  surface_h_ = static_cast<uint32_t>(h);
  has_resized_ = true;

  EVENTS_DISPATCH_SIGNAL(onResize, w, h);
}

// ----------------------------------------------------------------------------

bool Events::buttonDown(KeyCode_t button) const noexcept {
  return checkButtonState(button, [](KeyState state) {
    return (state == KeyState::Pressed) || (state == KeyState::Down);
  });
}

bool Events::buttonPressed(KeyCode_t button) const noexcept {
  return checkButtonState(button, [](KeyState state) {
    return (state == KeyState::Pressed);
  });
}

bool Events::buttonReleased(KeyCode_t button) const noexcept {
return checkButtonState(button, [](KeyState state) {
    return (state == KeyState::Released);
  });
}

bool Events::keyDown(KeyCode_t key) const noexcept {
  return checkKeyState(key, [](KeyState state) {
    return (state == KeyState::Pressed) || (state == KeyState::Down);
  });
}

bool Events::keyPressed(KeyCode_t key) const noexcept {
  return checkKeyState(key, [](KeyState state) {
    return (state == KeyState::Pressed);
  });
}

bool Events::keyReleased(KeyCode_t key) const noexcept {
return checkKeyState(key, [](KeyState state) {
    return (state == KeyState::Released);
  });
}

bool Events::gamepadButtonDown(KeyCode_t button) const noexcept {
  return checkGamepadButtonState(button, [](KeyState state) {
    return (state == KeyState::Pressed) || (state == KeyState::Down);
  });
}

bool Events::gamepadButtonPressed(KeyCode_t button) const noexcept {
  return checkGamepadButtonState(button, [](KeyState state) {
    return (state == KeyState::Pressed);
  });
}

bool Events::gamepadButtonReleased(KeyCode_t button) const noexcept {
return checkGamepadButtonState(button, [](KeyState state) {
    return (state == KeyState::Released);
  });
}

// ----------------------------------------------------------------------------

bool Events::checkButtonState(KeyCode_t button, StatePredicate_t predicate) const noexcept {
  if (auto search = buttons_.find(button); search != buttons_.end()) {
    return predicate(search->second);
  }
  return false;
}

bool Events::checkKeyState(KeyCode_t key, StatePredicate_t predicate) const noexcept {
  if (auto search = keys_.find(key); search != keys_.end()) {
    return predicate(search->second);
  }
  return false;
}

bool Events::checkGamepadButtonState(KeyCode_t button, StatePredicate_t predicate) const noexcept {
  if (auto search = gamepad_buttons_.find(button); search != gamepad_buttons_.end()) {
    return predicate(search->second);
  }
  return false;
}

/* -------------------------------------------------------------------------- */
