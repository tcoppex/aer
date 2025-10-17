#ifndef AER_PLATFORM_VULKAN_SWAPCHAIN_H_
#define AER_PLATFORM_VULKAN_SWAPCHAIN_H_

/* -------------------------------------------------------------------------- */

#include "aer/platform/vulkan/types.h"
class Context;

#include "aer/platform/swapchain_interface.h" //

/* -------------------------------------------------------------------------- */

class Swapchain : public SwapchainInterface {
 public:
  static constexpr uint32_t kPreferredMaxImageCount{ 3u };
  static constexpr bool kUseVSync{ true };
  static constexpr bool kKeepPreviousSwapchain{ true };

 public:
  Swapchain() = default;
  virtual ~Swapchain() = default;

  bool init(Context const& context, VkSurfaceKHR surface);

  void release(bool keep_previous_swapchain = false);

  [[nodiscard]]
  uint32_t swap_index() const noexcept {
    return swap_index_;
  }

  [[nodiscard]]
  std::vector<backend::Image> const& images() const noexcept {
    return images_;
  }

  [[nodiscard]]
  bool is_valid() const noexcept final {
    return need_rebuild_ == false;
  }

 public:
  [[nodiscard]]
  bool acquireNextImage() final;

  [[nodiscard]]
  bool submitFrame(VkQueue queue, VkCommandBuffer command_buffer) final;

  [[nodiscard]]
  bool finishFrame(VkQueue queue) final;

  [[nodiscard]]
  VkExtent2D surface_size() const noexcept final {
    return swapchain_create_info_.imageExtent;
  }

  [[nodiscard]]
  uint32_t image_count() const noexcept final {
    return image_count_;
  }

  [[nodiscard]]
  VkFormat format() const noexcept final {
    return images_[0u].format;
  }

  uint32_t view_mask() const noexcept final {
    return 0;
  }

  [[nodiscard]]
  backend::Image current_image() const noexcept final {
    return images_[acquired_image_index_];
  }

 private:
  [[nodiscard]]
  VkSurfaceFormat2KHR selectSurfaceFormat(
    VkPhysicalDeviceSurfaceInfo2KHR const* surface_info2
  ) const;

  [[nodiscard]]
  VkPresentModeKHR selectPresentMode(VkSurfaceKHR surface, bool use_vsync) const;

  [[nodiscard]]
  VkSemaphore wait_image_semaphore() const noexcept {
    return synchronizers_[swap_index_].wait_image_semaphore;
  }

  [[nodiscard]]
  VkSemaphore signal_present_semaphore() const noexcept {
    return synchronizers_[acquired_image_index_].signal_present_semaphore;
  }

  [[nodiscard]]
  uint64_t* timeline_signal_index_ptr() noexcept {
    return &timeline_.signal_indices[swap_index_];
  }

 private:
  struct Synchronizer {
    VkSemaphore wait_image_semaphore{};
    VkSemaphore signal_present_semaphore{};
  };

  struct Timeline {
    std::vector<uint64_t> signal_indices{};
    VkSemaphore semaphore{};
  };

  VkPhysicalDevice gpu_{};
  VkDevice device_{};

  VkSwapchainCreateInfoKHR swapchain_create_info_{};
  VkSwapchainKHR handle_{};

  std::vector<backend::Image> images_{};
  std::vector<Synchronizer> synchronizers_{};

  Timeline timeline_{};

  uint32_t image_count_{};  // max frames in flight
  uint32_t swap_index_{};
  uint32_t acquired_image_index_{};

  bool need_rebuild_ = true;
};

/* -------------------------------------------------------------------------- */

#endif
