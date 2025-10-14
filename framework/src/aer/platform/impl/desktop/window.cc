#if defined(_WIN32)
  #include <windows.h> // for SetConsoleOutputCP
#endif

#include "aer/platform/impl/desktop/window.h"
#include "aer/core/events.h"
#include "aer/core/logger.h"

/* -------------------------------------------------------------------------- */

#define WINDOW_SCALE          0.54f
#define MONITOR_SCALE         1.25f
#define WINDOW_SIZE_FACTOR    (WINDOW_SCALE / MONITOR_SCALE)

/* -------------------------------------------------------------------------- */

namespace {

void InitializeEventsCallbacks(GLFWwindow *handle) noexcept {
  // Keyboard.
  glfwSetKeyCallback(handle, [](GLFWwindow *window, int key, int scancode, int action, int mods) {
    auto &events = Events::Get();
    auto const k = static_cast<KeyCode_t>(key);

    if (action == GLFW_PRESS) {
      events.onKeyPressed(k);
    } else if (action == GLFW_RELEASE) {
      events.onKeyReleased(k);
    }
  });

  // Input Char.
  glfwSetCharCallback(handle, [](GLFWwindow*, unsigned int c) {
    if ((c > 0) && (c < 0x10000)) {
      Events::Get().onInputChar(static_cast<uint16_t>(c));
    }
  });

  // Mouse buttons.
  glfwSetMouseButtonCallback(handle, [](GLFWwindow* window, int button, int action, int mods) {
    auto &events = Events::Get();
    auto const mouse_x = events.mouseX();
    auto const mouse_y = events.mouseY();

    if (action == GLFW_PRESS) {
      events.onPointerDown(mouse_x, mouse_y, button);
    } else if (action == GLFW_RELEASE) {
      events.onPointerUp(mouse_x, mouse_y, button);
    }
  });

  // // Mouse Enter / Exit.
  // glfwSetCursorEnterCallback(handle, [](GLFWwindow* window, int entered) {
  //   auto &events = Events::Get();
  //   auto const mouse_x = events.mouseX();
  //   auto const mouse_y = events.mouseY();

  //   entered ? events.onPointerEntered(mouse_x, mouse_y)
  //           : events.onPointerExited(mouse_x, mouse_y)
  //           ;
  // });

  // Mouse motion.
  glfwSetCursorPosCallback(handle, [](GLFWwindow* window, double x, double y) {
    auto &events = Events::Get();
    auto const px = static_cast<int>(x);
    auto const py = static_cast<int>(y);

    events.onPointerMove(px, py);
  });

  // Mouse wheel.
  glfwSetScrollCallback(handle, [](GLFWwindow* window, double dx, double dy) {
    Events::Get().onMouseWheel(
      static_cast<float>(dx),
      static_cast<float>(dy)
    );
  });

  // // Drag-n-drop.
  // glfwSetDropCallback(handle, [](GLFWwindow* window, int count, char const** paths) {
  //   LOGW( "> glfwSetDropCallback is not implemented." );
  //   Events::Get().onFilesDropped(count, paths);
  // });

  // Framebuffer resize.
  glfwSetFramebufferSizeCallback(handle, [](GLFWwindow *window, int w, int h) {
    if ((w > 0) && (h > 0)) {
      Events::Get().onResize(w, h);
    } else {
      LOGW("Prevent framebuffer resize with null parameters ({}, {})", w, h);
    }
  });

  // Window resize.
  // glfwSetWindowSizeCallback(handle, [](GLFWwindow *window, int w, int h) {
  //   LOGW( "> glfwSetWindowSizeCallback is not implemented." );
  // });
}

} // namespace

/* -------------------------------------------------------------------------- */

bool Window::init(Settings const& settings, AppData_t app_data) {
#if defined(_WIN32)
  // Switch Windows console to UTF-8
  SetConsoleOutputCP(CP_UTF8);

  // Enable per-monitor DPI awareness
  // SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  // Force stderr to flush automatically on Windows.
  std::setbuf(stderr, nullptr);
#endif

  if (!glfwInit()) {
    LOGE("Could not initialize GLFW.\n");
    return false;
  }
  if (!glfwVulkanSupported()) {
    LOGE("Vulkan is not supported.\n");
    return false;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  GLFWmonitor* monitor = glfwGetPrimaryMonitor();
  {
    float xscale, yscale;
    glfwGetMonitorContentScale(monitor, &xscale, &yscale);
    dpi_scale_ = xscale;

    GLFWvidmode const* mode = glfwGetVideoMode(monitor);

    if (settings.width > 0u) {
      surface_w_ = settings.width;
    } else {
      surface_w_ = static_cast<uint32_t>(WINDOW_SIZE_FACTOR * dpi_scale_ * mode->width);
    }
    if (settings.height > 0u) {
      surface_h_ = settings.height;
    } else {
      surface_h_ = static_cast<uint32_t>(WINDOW_SIZE_FACTOR * dpi_scale_ * mode->height);
    }
  }

  window_ = glfwCreateWindow(surface_w_, surface_h_, "", nullptr, nullptr);

  if (!window_) {
    LOGE("GLFW couldn't create the window.\n");
    glfwTerminate();
    return false;
  }

  // Do not constraints aspect ratio.
  glfwSetWindowAspectRatio(window_, GLFW_DONT_CARE, GLFW_DONT_CARE);

  // ~Silverish / Japanese JIS B6 ratio.
  // glfwSetWindowAspectRatio(window_, 91, 64); // ~ 1.422

  // Other ratios.
  // glfwSetWindowAspectRatio(window_, 707, 500); // ~1.414
  // glfwSetWindowAspectRatio(window_, 866, 500); // ~1.732

  InitializeEventsCallbacks(window_);

  return true;
}

// ----------------------------------------------------------------------------

void Window::shutdown() {
  glfwTerminate();
}

// ----------------------------------------------------------------------------

bool Window::poll(AppData_t app_data) noexcept {
  glfwPollEvents();

  // Hardcode exit via escape key.
  if (Events::Get().keyPressed(GLFW_KEY_ESCAPE)) {
    close();
  }

  return GLFW_TRUE != glfwWindowShouldClose(window_);
}

// ----------------------------------------------------------------------------

void Window::setTitle(std::string_view title) const noexcept {
  glfwSetWindowTitle(window_, title.data());
}

// ----------------------------------------------------------------------------

void Window::close() noexcept {
  glfwSetWindowShouldClose(window_, GLFW_TRUE);
}

// ----------------------------------------------------------------------------

bool Window::isActive() const noexcept {
  return glfwGetWindowAttrib(window_, GLFW_ICONIFIED) == GLFW_FALSE;
}

// ----------------------------------------------------------------------------

std::vector<char const*> Window::vulkanInstanceExtensions() const noexcept {
  uint32_t extension_count;
  auto extensions = glfwGetRequiredInstanceExtensions(&extension_count);

  std::vector<char const*> result{};
  result.insert(result.begin(), extensions, extensions + extension_count);

  return result;
}

// ----------------------------------------------------------------------------

VkResult Window::createWindowSurface(VkInstance instance, VkSurfaceKHR *surface) const noexcept {
  return glfwCreateWindowSurface(instance, window_, nullptr, surface);
}

/* -------------------------------------------------------------------------- */
