#ifndef AER_PLATEFORM_COMMON_H_
#define AER_PLATEFORM_COMMON_H_

/* -------------------------------------------------------------------------- */

extern "C" {

#if defined(ANDROID)
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android/keycodes.h>
#endif

//(should this be best linked to the app instead of the framework?)
#if defined(ANDROID)
#include <android_native_app_glue.h>
#endif

}

#if defined(USE_OPENXR)
#include "aer/platform/openxr/openxr_context.h"
#endif

/* -------------------------------------------------------------------------- */
// -- Structures

#if defined(ANDROID)
using AppData_t = struct android_app*;
#else
using AppData_t = void*;
#endif

// [wip] Android user data for AppData_t->userData.
struct UserData {
  void *self{};
  XRInterface *xr{};
};

// [used only by Android app]
struct AppCmdCallbacks {
  virtual ~AppCmdCallbacks() {}
  virtual void onInitWindow(AppData_t app) {}
  virtual void onTermWindow(AppData_t app) {}
  virtual void onWindowResized(AppData_t app) {}
  virtual void onStart(AppData_t app) {}
  virtual void onResume(AppData_t app) {}
  virtual void onPause(AppData_t app) {}
  virtual void onStop(AppData_t app) {}
  virtual void onGainedFocus(AppData_t app) {}
  virtual void onLostFocus(AppData_t app) {}
  virtual void onSaveState(AppData_t app) {}
  virtual void onDestroy(AppData_t app) {}
};

/* -------------------------------------------------------------------------- */
// -- Macros

#if !defined(AER_USE_OPENXR)
#define AER_USE_OPENXR 0
#endif

#if defined(ANDROID)

#define ENTRY_POINT(AppClass)                                           \
extern "C" {                                                            \
  void android_main(struct android_app* app_data) {                     \
    std::unique_ptr<Application> app = std::make_unique<AppClass>();    \
    auto settings = app->settings();                                    \
    settings.use_xr = bool(AER_USE_OPENXR);                             \
    app->run(settings, app_data);                                       \
  }                                                                     \
}

#else // DESKTOP

#define ENTRY_POINT(AppClass)                                           \
extern "C" {                                                            \
  int main(int argc, char *argv[]) {                                    \
    std::unique_ptr<Application> app = std::make_unique<AppClass>();    \
    auto settings = app->settings();                                    \
    settings.use_xr = bool(AER_USE_OPENXR);                             \
    return app->run(settings);                                          \
  }                                                                     \
}

#endif

/* -------------------------------------------------------------------------- */

#endif