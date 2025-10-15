#ifndef AER_APPLICATION_H_
#define AER_APPLICATION_H_

/* -------------------------------------------------------------------------- */

#include <chrono>
using namespace std::chrono_literals;

#include "aer/settings.h"
#include "aer/core/common.h"
#include "aer/core/event_callbacks.h"

#include "aer/platform/common.h"
#include "aer/platform/wm_interface.h"
#include "aer/platform/ui_controller.h"

#include "aer/platform/swapchain_interface.h"
#include "aer/platform/vulkan/swapchain.h" //

#include "aer/renderer/render_context.h"
#include "aer/renderer/renderer.h"

/* -------------------------------------------------------------------------- */

class Application : public EventCallbacks
                  , public AppCmdCallbacks {
 public:
  Application() = default;

  virtual ~Application() = default;

  virtual AppSettings settings() const noexcept {
    return {};
  }

  int run(AppSettings const& app_settings, AppData_t app_data = {});

 protected:
  virtual bool setup() {
    return true;
  }

  virtual void release() {}

  [[nodiscard]]
  virtual std::vector<char const*> xrExtensions() const noexcept {
    return {};
  }

  virtual void build_ui() {}

  virtual void update(float const dt) {}

  virtual void draw(CommandEncoder const& cmd) {}

 protected:
  [[nodiscard]]
  float elapsed_time() const noexcept;

  [[nodiscard]]
  float frame_time() const noexcept {
    return frame_time_;
  }

  [[nodiscard]]
  float delta_time() const noexcept {
    return frame_time_ - last_frame_time_;
  }

  void draw_ui(CommandEncoder const& cmd);

 private:
  [[nodiscard]]
  bool presetup(AppData_t app_data);

  [[nodiscard]]
  bool next_frame(AppData_t app_data);

  void update_timer() noexcept;

  void update_ui() noexcept;

  void mainloop(AppData_t app_data);

  bool reset_swapchain();

  void shutdown();

 protected:
  std::unique_ptr<WMInterface> wm_{};
  std::unique_ptr<OpenXRContext> xr_{};
  std::unique_ptr<UIController> ui_{};

  RenderContext context_{};
  Renderer renderer_{};

  VkExtent2D viewport_size_{}; // (to be removed)

 private:
  AppSettings settings_{};

  SwapchainInterface *swapchain_interface_{};
  std::unique_ptr<EventCallbacks> default_callbacks_{};

  // [non-XR only]
  VkSurfaceKHR surface_{};
  Swapchain swapchain_{};

  // |Android only]
  UserData user_data_{};

  // [Time tracker]
  std::chrono::time_point<std::chrono::high_resolution_clock> chrono_{};
  float frame_time_{};
  float last_frame_time_{};

  uint32_t rng_seed_{};
};

/* -------------------------------------------------------------------------- */

#endif