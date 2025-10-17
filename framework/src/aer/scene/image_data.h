#ifndef AER_SCENE_IMAGE_DATA_H_
#define AER_SCENE_IMAGE_DATA_H_

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuseless-cast"
#pragma clang diagnostic ignored "-Wcast-align"
#pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuseless-cast"
#pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#else
#pragma warning(push)
#endif

extern "C" {
#include <stb/stb_image.h>
}

#ifdef __clang__
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#else
#pragma warning(pop)
#endif

#include "aer/core/common.h"
#include "aer/core/utils.h"

namespace scene {

/* -------------------------------------------------------------------------- */

struct ImageData {
 public:
  static constexpr int32_t kDefaultNumChannels{ STBI_rgb_alpha }; //

 public:
  ImageData() = default;

  ImageData(uint8_t r, uint8_t g, uint8_t b, uint8_t a, int32_t _width = 1, int32_t _height = 1) {
    width = _width;
    height = _height;
    channels = kDefaultNumChannels;
    auto* data = static_cast<uint8_t*>(malloc(width * height * channels));
    int index(0);
    for (int j=0; j<height; ++j) {
      for (int i=0; i<width; ++i, ++index) {
        data[index+0] = r;
        data[index+1] = g;
        data[index+2] = b;
        data[index+3] = a;
      }
    }
    pixels_.reset(data);
  }

  bool load(stbi_uc const* buffer_data, uint32_t const buffer_size) {
    auto pixels_data = stbi_load_from_memory(
      buffer_data,
      static_cast<int32_t>(buffer_size),
      &width,
      &height,
      &channels,
      kDefaultNumChannels
    );
    if (pixels_data) {
      pixels_.reset(pixels_data);
      comp_bytesize_ = 1u;
    }
    return nullptr != pixels_data;
  }

  bool loadf(stbi_uc const* buffer_data, uint32_t const buffer_size) {
    auto pixels_data = reinterpret_cast<stbi_uc*>(stbi_loadf_from_memory(
      buffer_data,
      static_cast<int32_t>(buffer_size),
      &width,
      &height,
      &channels,
      kDefaultNumChannels
    ));

    if (pixels_data) {
      pixels_.reset(pixels_data);
      comp_bytesize_ = 4u;
    }
    return nullptr != pixels_data;
  }

  void release() {
    pixels_.reset();
  }

  std::future<bool> asyncLoadFuture(stbi_uc const* buffer_data, uint32_t const buffer_size) {
    if (retrieveImageInfo(buffer_data, buffer_size)) {
      return utils::RunTaskGeneric<bool>([this, buffer_data, buffer_size] {
        return load(buffer_data, buffer_size);
      });
    }
    return {};
  }

  void asyncLoad(stbi_uc const* buffer_data, uint32_t const buffer_size) {
    if (retrieveImageInfo(buffer_data, buffer_size)) {
      async_result_ = utils::RunTaskGeneric<bool>([this, buffer_data, buffer_size] {
        return load(buffer_data, buffer_size);
      });
    }
  }

  bool async_load_result() {
    return async_result_.valid() ? async_result_.get() : false;
  }

  uint8_t const* pixels() {
    return (pixels_ || (async_load_result() && pixels_)) ? pixels_.get() : nullptr;
  }

  uint8_t const* pixels() const {
    return pixels_.get();
  }

  uint32_t bytesize() const {
    return static_cast<uint32_t>(kDefaultNumChannels * width * height * comp_bytesize_);
  }

 public:
  int32_t width{};
  int32_t height{};
  int32_t channels{};

 private:
  bool retrieveImageInfo(stbi_uc const *buffer_data, int buffer_size) {
    return 0 < stbi_info_from_memory(buffer_data, buffer_size, &width, &height, &channels);
  }

  std::unique_ptr<uint8_t, decltype(&stbi_image_free)> pixels_{nullptr, stbi_image_free}; //
  std::future<bool> async_result_;
  uint32_t comp_bytesize_{1u};
};

/* -------------------------------------------------------------------------- */

}  // namespace scene

#endif // AER_SCENE_IMAGE_DATA_H_