#ifndef AER_SCENE_ANIMATION_POSE_CACHE_H_
#define AER_SCENE_ANIMATION_POSE_CACHE_H_

#include <memory>
#include <utility>
#include <vector>

#include "aer/scene/animation.h"

/* -------------------------------------------------------------------------- */

namespace scene {

//
//    PoseCache
//
//  Pool of poses for temporary use (scratch, snapshots).
//
class PoseCache {
 public:
  static constexpr size_t kMaxFree = 64u;

 private:
  struct Pool {
    std::vector<std::unique_ptr<Pose>> free{};

    void release(std::unique_ptr<Pose> pose) {
      if (free.size() < kMaxFree) {
        free.push_back(std::move(pose));
      }
    }
  };

 public:
  class Lease {
   public:
    Lease() = default;
    ~Lease() { reset(); }

    Lease(Lease const&) = delete;
    Lease& operator=(Lease const&) = delete;

    Lease(Lease&&) noexcept = default;
    Lease& operator=(Lease&& other) noexcept {
      if (this != &other) {
        reset();
        pool_ = std::move(other.pool_);
        pose_ = std::move(other.pose_);
      }
      return *this;
    }

    explicit operator bool() const noexcept { return pose_ != nullptr; }

    Pose& operator*() noexcept { return *pose_; }
    Pose const& operator*() const noexcept { return *pose_; }
    Pose* operator->() noexcept { return pose_.get(); }

    void reset() {
      if (pool_ && pose_) {
        pool_->release(std::move(pose_));
      }
      pool_.reset();
      pose_.reset();
    }

   private:
    friend class PoseCache;

    std::shared_ptr<Pool> pool_{};
    std::unique_ptr<Pose> pose_{};
  };

 public:
  [[nodiscard]]
  Lease acquire(size_t joint_count) {
    Lease lease{};
    lease.pool_ = pool_;

    if (!pool_->free.empty()) {
      lease.pose_ = std::move(pool_->free.back());
      pool_->free.pop_back();
    } else {
      lease.pose_ = std::make_unique<Pose>();
    }
    lease.pose_->joints.resize(joint_count);
    return lease;
  }

  /* Poses waiting in the pool (for debugging). */
  [[nodiscard]]
  size_t freeCount() const noexcept { return pool_->free.size(); }

 private:
  std::shared_ptr<Pool> pool_{std::make_shared<Pool>()};
};

}  // namespace "scene"

/* -------------------------------------------------------------------------- */

#endif // AER_SCENE_ANIMATION_POSE_CACHE_H_
