#ifndef AER_CORE_UTILS_H_
#define AER_CORE_UTILS_H_

/* -------------------------------------------------------------------------- */
//
//    utils.h
//
//  A bunch of utility functions, not yet organized to be elsewhere.
//
/* -------------------------------------------------------------------------- */

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <string_view>
#include <vector>
#include <future>
#include <functional>

/* -------------------------------------------------------------------------- */

namespace utils {

// --- structs ---

struct FileReader {
  static constexpr std::size_t kMaxReadFileSize = 4 << 27; // ~512Mib

  FileReader() = default;

  static
  bool Read(std::string_view filename, std::vector<uint8_t>& out);

  bool read(std::string_view filename) {
    return FileReader::Read(filename, buffer);
  }

  void clear() {
    buffer.clear();
  }

  std::vector<uint8_t> buffer;
};

// --- constexpr functions ---

constexpr uint32_t Log2_u32(uint32_t x) {
  uint32_t result = 0;
  while (x > 1) {
    x >>= 1;
    ++result;
  }
  return result;
}

// --- template functions ---

size_t HashCombine(size_t seed, auto const& value) {
  return seed ^ (std::hash<std::decay_t<decltype(value)>>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <typename T>
inline auto RunTaskGeneric = [](auto&& fn) -> std::future<T> {
  return std::async(std::launch::async, std::forward<decltype(fn)>(fn));
};

template<typename T>
std::vector<std::byte> ToBytes(const T& value) {
  static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
  std::vector<std::byte> buffer(sizeof(T));
  std::memcpy(buffer.data(), &value, sizeof(T));
  return buffer;
}

// --- functions ---

// char* ReadBinaryFile(const char* filename, size_t* filesize);

std::string ExtractBasename(std::string_view filename, bool keepExtension = false);

std::string ExtractExtension(std::string_view filename);

size_t AlignTo(size_t const byteLength, size_t const byteAlignment);

size_t AlignTo256(size_t const byteLength);

// ----------------------------------------------------------------------------

} // namespace "utils"

/* -------------------------------------------------------------------------- */

#endif