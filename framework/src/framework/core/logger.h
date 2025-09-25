#ifndef VKFRAMEWORK_CORE_LOGGER_H
#define VKFRAMEWORK_CORE_LOGGER_H

/* -------------------------------------------------------------------------- */

#include "framework/core/singleton.h"

extern "C" {
#if defined(ANDROID)
#include <android/log.h>
#endif
}

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include "fmt/core.h" // (c++20 format require gcc13+)

/* -------------------------------------------------------------------------- */

//
// A colored logger that could be used inside loops to print messages once.
//
//  Type of logs :
//    * Message    : white, not hashed (will be repeated).
//    * Info       : blue, hashed (will not be repeated).
//    * Warning    : yellow, hashed, used in stats.
//    * Error      : bold red, hashed, display file and line, used in stats.
//    * FatalError : flashing red, not hashed, exit program instantly.
//
class Logger : public Singleton<Logger> {
  friend class Singleton<Logger>;

 public:
  static std::string TrimFilename(std::string const& filename) {
    return filename.substr(filename.find_last_of("/\\") + 1);
  }

  enum class LogType {
    Message,
    Info,
    Warning,
    Error,
    FatalError
  };

  ~Logger() {
#ifndef NDEBUG
    displayStats();
#endif // NDEBUG
  }

  template<typename... Args>
  bool log(
    char const* file,
    char const* fn,
    int line,
    bool useHash,
    LogType type,
    std::string_view fmt,
    Args&&... args
  ) {
    // Clear the local stream and retrieve the full current message.
    out_.str({});
    out_ << fmt::vformat(fmt, fmt::make_format_args(std::forward<Args>(args)...));

    // Trim filename for display.
    std::string filename(file);
    filename = filename.substr(filename.find_last_of("/\\") + 1);

    // Check the message has not been registered yet.
    if (useHash) {
      auto const key = std::string(/*filename + std::to_string(line) +*/ out_.str());
      if (0u < error_log_.count(key)) {
        return false;
      }
      error_log_[key] = true;
    }

    // Prefix.
    switch (type) {
      case LogType::Message:    std::cerr << "\x1b[0;29m";
        break;
      case LogType::Info:       std::cerr << "\x1b[0;36m";
        break;
      case LogType::Warning:    std::cerr << "\x1b[3;33m";
        ++warning_count_;
        break;
      case LogType::Error:      std::cerr << "\x1b[1;31m[Error] ";
        ++error_count_;
        break;
      case LogType::FatalError: std::cerr << "\x1b[5;31m[Fatal Error]\x1b[0m\n\x1b[0;31m ";
        break;
    }

    std::cerr << out_.str();

    // Suffix.
    switch (type) {
      case LogType::Error:
      case LogType::FatalError:
        std::cerr <<  "\n(" << filename << " " << fn << " L." << line << ")\n";
        break;

      default:
        break;
    }

    std::cerr << "\x1b[0m\n";

    return true;
  }

  template<typename... Args>
  void android_log(
    int priority,
    char const* tag,
    fmt::format_string<Args...> fmt,
    Args&&... args
  ) {
#if defined(ANDROID)
    auto const msg = fmt::format(fmt, std::forward<Args>(args)...);
    __android_log_print(priority, tag, "%s", msg.c_str());
#endif
  }

  template<typename... Args>
  void message(char const* file, char const* fn, int line, fmt::format_string<Args...> fmt, Args&&... args) {
    log(file, fn, line, false, LogType::Message, fmt::vformat(fmt, fmt::make_format_args(args...)));
  }

  template<typename... Args>
  void info(char const* file, char const* fn, int line, fmt::format_string<Args...> fmt, Args&&... args) {
    log(file, fn, line, true, LogType::Info, fmt::vformat(fmt, fmt::make_format_args(args...)));
  }

  template<typename... Args>
  void warning(char const* file, char const* fn, int line, std::string_view fmt, Args&&... args) {
    log(file, fn, line, true, LogType::Warning, fmt::vformat(fmt, fmt::make_format_args(args...)));
  }

  template<typename... Args>
  void error(char const* file, char const* fn, int line, fmt::format_string<Args...> fmt, Args&&... args) {
    log(file, fn, line, true, LogType::Error, fmt::vformat(fmt, fmt::make_format_args(args...)));
  }

  template<typename... Args>
  void fatal_error(char const* file, char const* fn, int line, fmt::format_string<Args...> fmt, Args&&... args) {
    log(file, fn, line, false, LogType::FatalError, fmt::vformat(fmt, fmt::make_format_args(args...)));
    //throw std::runtime_error("Fatal error");
    // std::terminate();
    std::exit(EXIT_FAILURE);
  }

 private:
  void displayStats() {
    if ((warning_count_ > 0) || (error_count_ > 0)) {
      std::cerr << "\n"
        "\x1b[7;38m================= Logger stats =================\x1b[0m\n" \
        " * Warnings : " << warning_count_ << std::endl <<
        " * Errors   : " << error_count_ << std::endl <<
        "\x1b[7;38m================================================\x1b[0m\n\n"
        ;
    }
  }

  std::stringstream out_{};
  std::unordered_map<std::string, bool> error_log_{};
  int32_t warning_count_{};
  int32_t error_count_{};
};

/* -------------------------------------------------------------------------- */

#if defined(ANDROID) && !defined(LOGGER_ANDROID_TAG)
#define LOGGER_ANDROID_TAG "VkFramework"
#endif

#if defined(ANDROID)
#define LOGV(...) Logger::Get().android_log(ANDROID_LOG_VERBOSE, LOGGER_ANDROID_TAG, __VA_ARGS__)
#define LOGD(...) Logger::Get().android_log(ANDROID_LOG_DEBUG,   LOGGER_ANDROID_TAG, __VA_ARGS__)
#define LOGI(...) Logger::Get().android_log(ANDROID_LOG_INFO,    LOGGER_ANDROID_TAG, __VA_ARGS__)
#define LOGW(...) Logger::Get().android_log(ANDROID_LOG_WARN,    LOGGER_ANDROID_TAG, __VA_ARGS__)
#define LOGE(...) Logger::Get().android_log(ANDROID_LOG_ERROR,   LOGGER_ANDROID_TAG, __VA_ARGS__)
#else
#define LOGV(...) Logger::Get().message( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGD(...) Logger::Get().message( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGI(...) Logger::Get().info   ( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGW(...) Logger::Get().warning( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGE(...) Logger::Get().error  ( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#endif

#define LOG_FATAL(...) LOGE(__VA_ARGS__); exit(-1)

// ----------------------------------------------------------------------------

#ifdef NDEBUG

#undef LOGV
#define LOGV(...)

#undef LOGD
#define LOGD(...)

#endif

/* -------------------------------------------------------------------------- */

#define LOG_CHECK(x) assert(x)

/* -------------------------------------------------------------------------- */

#endif // VKFRAMEWORK_CORE_LOGGER_H