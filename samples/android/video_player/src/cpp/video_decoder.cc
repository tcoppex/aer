#include "video_decoder.h"

// ----------------------------------------------------------------------------

#include <aer/core/logger.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <cstring>

/* -------------------------------------------------------------------------- */

VideoDecoder::~VideoDecoder() {
  stop();
}

// ----------------------------------------------------------------------------

bool VideoDecoder::start(std::string const& url) {
  extractor_ = AMediaExtractor_new();

  if ( !extractor_
    || (AMediaExtractor_setDataSource(extractor_, url.c_str()) != AMEDIA_OK))
  {
    LOGE("Failed to open video: {}", url);
    if (extractor_) {
      AMediaExtractor_delete(extractor_);
    }
    extractor_ = nullptr;
    return false;
  }

  // Find video format.
  int const track_count = AMediaExtractor_getTrackCount(extractor_);
  for (int i = 0; i < track_count; ++i) {
    AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor_, i);
    const char* mime = nullptr;

    if ( format
      && AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)
      && mime
      && (strncmp(mime, "video/", 6) == 0))
    {
      video_stream_idx_ = i;
      AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, reinterpret_cast<int32_t*>(&width_));
      AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, reinterpret_cast<int32_t*>(&height_));
      video_format_ = format;
      break;
    }
    if (format) {
      AMediaFormat_delete(format);
    }
  }
  if (!video_format_ || video_stream_idx_ < 0 || width_ == 0 || height_ == 0) {
    LOGE("No valid video track found");
    stop();
    return false;
  }

  if (AMediaExtractor_selectTrack(extractor_, video_stream_idx_) != AMEDIA_OK) {
    LOGE("Failed to select video track");
    stop();
    return false;
  }

  media_status_t status = AImageReader_newWithUsage(
    width_,
    height_,
    AIMAGE_FORMAT_PRIVATE,
    AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
    4,
    &image_reader_
  );
  if (status != AMEDIA_OK || !image_reader_) {
    LOGE("AImageReader creation failed: {}", (int)status);
    stop();
    return false;
  }

  ANativeWindow* window{};
  AImageReader_getWindow(image_reader_, &window);

  const char* mime{};
  AMediaFormat_getString(video_format_, AMEDIAFORMAT_KEY_MIME, &mime);
  media_codec_ = mime ? AMediaCodec_createDecoderByType(mime) : nullptr;

  if (!media_codec_) {
    LOGE("Failed to create video decoder");
    stop();
    return false;
  }

  status = AMediaCodec_configure(media_codec_, video_format_, window, nullptr, 0);

  if (status != AMEDIA_OK) {
    LOGE("AMediaCodec_configure failed: {}", (int)status);
    stop();
    return false;
  }

  // ---------

  status = AMediaCodec_start(media_codec_);
  if (status != AMEDIA_OK) {
    LOGE("AMediaCodec_start failed: {}", (int)status);
    stop();
    return false;
  }
  is_running_ = true;
  decoder_thread_ = std::thread(&VideoDecoder::decodeTask, this);

  // ---------

  return true;
}

// ----------------------------------------------------------------------------

void VideoDecoder::stop() {
  is_running_ = false;
  if (decoder_thread_.joinable()) {
    decoder_thread_.join();
  }
  if (video_format_) {
    AMediaFormat_delete(video_format_);
    video_format_ = nullptr;
  }
  if (media_codec_) {
    AMediaCodec_stop(media_codec_);
    AMediaCodec_delete(media_codec_);
    media_codec_ = nullptr;
  }
  if (image_reader_) {
    AImageReader_delete(image_reader_);
    image_reader_ = nullptr;
  }
  if (extractor_) {
    AMediaExtractor_delete(extractor_);
    extractor_ = nullptr;
  }
}

// ----------------------------------------------------------------------------

void VideoDecoder::set_pause(bool paused) {
  is_paused_ = paused;
  if (!paused) {
    playback_clock_started_ = false;
  }
}

// ----------------------------------------------------------------------------

void VideoDecoder::seek(double time_seconds) {
  if (!extractor_ || video_stream_idx_ < 0) {
    return;
  }
  int64_t timestamp_us = static_cast<int64_t>(time_seconds * 1000000.0);
  AMediaExtractor_seekTo(
    extractor_, timestamp_us, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC
  );
  if (media_codec_) {
    AMediaCodec_flush(media_codec_);
  }
}

// ----------------------------------------------------------------------------

void VideoDecoder::decodeTask() {
  bool input_eos = false;

  while (is_running_) {
    if (is_paused_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }

    if (!input_eos) {
      ssize_t input_idx = AMediaCodec_dequeueInputBuffer(media_codec_, 10000);

      if (input_idx >= 0) {
        size_t capacity = 0;
        uint8_t* buffer = AMediaCodec_getInputBuffer(
          media_codec_, input_idx, &capacity
        );

        ssize_t sample_size = buffer
                            ?  AMediaExtractor_readSampleData(extractor_, buffer, capacity)
                            : -1
                            ;

        int64_t pts = AMediaExtractor_getSampleTime(extractor_);

        if (sample_size < 0 || pts < 0) {
          AMediaCodec_queueInputBuffer(
            media_codec_, input_idx, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM
          );
          input_eos = true;
        } else {
          uint32_t flags = AMediaExtractor_getSampleFlags(extractor_);

          media_status_t status = AMediaCodec_queueInputBuffer(
            media_codec_, input_idx, 0, static_cast<size_t>(sample_size), pts, flags
          );

          if (status == AMEDIA_OK) {
            AMediaExtractor_advance(extractor_);
          } else {
            LOGE("queueInputBuffer failed: {}", (int)status);
          }
        }
      }
    }

    AMediaCodecBufferInfo info{};
    ssize_t output_idx = AMediaCodec_dequeueOutputBuffer(media_codec_, &info, 10000);

    if (output_idx >= 0) {
      bool render = (info.size > 0);

      if (render) {
        // Establish the playback clock on the first frame.
        if (!playback_clock_started_) {
          playback_start_time_ = std::chrono::steady_clock::now();
          playback_start_pts_us_ = info.presentationTimeUs;
          playback_clock_started_ = true;
        }

        // Calculate when this frame should be displayed.
        auto target_time = playback_start_time_ +
          std::chrono::microseconds(info.presentationTimeUs - playback_start_pts_us_);

        auto now = std::chrono::steady_clock::now();

        if (target_time > now) {
          std::this_thread::sleep_until(target_time);
        }
      }

      AMediaCodec_releaseOutputBuffer(media_codec_, output_idx, render);

      if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
        break;
      }
    }
  }
}

// ----------------------------------------------------------------------------

AHardwareBuffer* VideoDecoder::acquireLatestFrame(AImage** out_image) {
  if (!out_image) {
    return nullptr;
  }
  *out_image = nullptr;

  if (!image_reader_) {
    return nullptr;
  }
  AImage* image = nullptr;

  media_status_t status = AImageReader_acquireLatestImage(image_reader_, &image);

  if (status == AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE) {
    return nullptr;
  }

  if (status != AMEDIA_OK || !image) {
    LOGE("acquireLatestImage failed: {}", (int)status);
    return nullptr;
  }

  AHardwareBuffer* buffer = nullptr;
  status = AImage_getHardwareBuffer(image, &buffer);

  if (status != AMEDIA_OK || !buffer) {
    LOGE("AImage_getHardwareBuffer failed: {}", (int)status);
    AImage_delete(image);
    return nullptr;
  }

  *out_image = image;

  return buffer;
}

// ----------------------------------------------------------------------------

void VideoDecoder::releaseImage(AImage* image) {
  if (image) {
    AImage_delete(image);
  }
}

/* -------------------------------------------------------------------------- */