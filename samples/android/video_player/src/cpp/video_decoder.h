#pragma once

// ----------------------------------------------------------------------------

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

struct AHardwareBuffer;
struct AImageReader;
struct AImage;
struct AMediaExtractor;
struct AMediaCodec;
struct AMediaFormat;

/* -------------------------------------------------------------------------- */

class VideoDecoder {
 public:
  VideoDecoder() = default;
  ~VideoDecoder();

  bool start(std::string const& url);

  void stop();

  void seek(double time_seconds);

  [[nodiscard]]
  AHardwareBuffer* acquireLatestFrame(AImage** out_image);

  void releaseImage(AImage* image);

  [[nodiscard]]
  uint32_t width() const noexcept {
    return width_;
  }

  [[nodiscard]]
  uint32_t height() const noexcept {
    return height_;
  }


  [[nodiscard]]
  bool is_running() const noexcept {
    return is_running_;
  }

  [[nodiscard]]
  bool is_paused() const noexcept {
    return is_paused_;
  }

  void set_pause(bool paused);

 private:
  void decodeTask();

 private:
  AMediaExtractor *extractor_{nullptr};

  int video_stream_idx_{-1};
  AMediaFormat* video_format_{nullptr};

  AMediaCodec *media_codec_{nullptr};
  AImageReader *image_reader_{nullptr};

  uint32_t width_{0};
  uint32_t height_{0};

  std::thread decoder_thread_;
  std::atomic<bool> is_running_{false};
  std::atomic<bool> is_paused_{false};

  std::chrono::steady_clock::time_point playback_start_time_;
  int64_t playback_start_pts_us_{0};
  bool playback_clock_started_{false};
};

/* -------------------------------------------------------------------------- */