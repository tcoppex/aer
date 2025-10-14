#ifndef AER_CORE_EVENT_CALLBACKS_H_
#define AER_CORE_EVENT_CALLBACKS_H_

#include "aer/core/common.h"

/* -------------------------------------------------------------------------- */

struct EventCallbacks;

using KeyCode_t = uint16_t;
using EventCallbacksPtr = EventCallbacks*;

// ----------------------------------------------------------------------------

struct EventCallbacks {
  virtual ~EventCallbacks() = default;

  virtual void onKeyPressed(KeyCode_t key) {}

  virtual void onKeyReleased(KeyCode_t key) {}

  virtual void onInputChar(uint16_t c) {}

  virtual void onPointerDown(int x, int y, KeyCode_t button) {}

  virtual void onPointerUp(int x, int y, KeyCode_t button) {}

  virtual void onPointerMove(int x, int y) {}

  virtual void onMouseWheel(float dx, float dy) {}

  virtual void onResize(int w, int h) {}
};

/* -------------------------------------------------------------------------- */

#endif  // AER_CORE_EVENT_CALLBACKS_H_
