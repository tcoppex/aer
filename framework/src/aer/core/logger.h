#ifndef AER_CORE_LOGGER_H_
#define AER_CORE_LOGGER_H_

/* -------------------------------------------------------------------------- */

#include <cassert>

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#if defined(ANDROID)

extern "C" {
#include <android/log.h>
}

#if !defined(LOGGER_ANDROID_TAG)
#define LOGGER_ANDROID_TAG "VkFramework"
#endif

#endif

#include "fmt/core.h" // (c++20 format require gcc13+)

#include "aer/core/singleton.h"

/* -------------------------------------------------------------------------- */

//
// A colored logger that could be used inside loops to print messages once.
//
//  Type of logs :
//    * Debug      : white, not hashed (will be repeated).
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
    Verbose,
    Debug,
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

#if defined(ANDROID)
    int AndroidLogType{};
    switch (type) {
      case LogType::Verbose:
        AndroidLogType = ANDROID_LOG_VERBOSE;
      break;
      case LogType::Debug:
        AndroidLogType = ANDROID_LOG_DEBUG;
      break;
      case LogType::Info:
        AndroidLogType = ANDROID_LOG_INFO;
      break;
      case LogType::Warning:
        AndroidLogType = ANDROID_LOG_WARN;
        ++warning_count_;
      break;
      case LogType::Error:
        AndroidLogType = ANDROID_LOG_ERROR;
        ++error_count_;
      break;
      case LogType::FatalError:
        AndroidLogType = ANDROID_LOG_ERROR; //
      break;
    }
    __android_log_print(AndroidLogType, LOGGER_ANDROID_TAG, "%s", out_.str().data());
    return true;
#endif

    // Prefix.
    switch (type) {
      case LogType::Verbose:    std::cerr << "\x1b[3;38;5;109m";
        break;
      case LogType::Debug:      std::cerr << "\x1b[0;39m";
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

//   template<typename... Args>
//   void android_log(
//     int priority,
//     char const* tag,
//     fmt::format_string<Args...> fmt,
//     Args&&... args
//   ) {
// #if defined(ANDROID)
//     auto const msg = fmt::format(fmt, std::forward<Args>(args)...);
//     __android_log_print(priority, tag, "%s", msg.c_str());
// #endif
//   }

  template<typename... Args>
  void verbose(char const* file, char const* fn, int line, fmt::format_string<Args...> fmt, Args&&... args) {
    log(file, fn, line, false, LogType::Verbose, fmt::vformat(fmt, fmt::make_format_args(args...)));
  }

  template<typename... Args>
  void debug(char const* file, char const* fn, int line, fmt::format_string<Args...> fmt, Args&&... args) {
    log(file, fn, line, false, LogType::Debug, fmt::vformat(fmt, fmt::make_format_args(args...)));
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


// #if defined(ANDROID)
// #define LOGV(...) Logger::Get().android_log(ANDROID_LOG_VERBOSE, LOGGER_ANDROID_TAG, __VA_ARGS__)
// #define LOGD(...) Logger::Get().android_log(ANDROID_LOG_DEBUG,   LOGGER_ANDROID_TAG, __VA_ARGS__)
// #define LOGI(...) Logger::Get().android_log(ANDROID_LOG_INFO,    LOGGER_ANDROID_TAG, __VA_ARGS__)
// #define LOGW(...) Logger::Get().android_log(ANDROID_LOG_WARN,    LOGGER_ANDROID_TAG, __VA_ARGS__)
// #define LOGE(...) Logger::Get().android_log(ANDROID_LOG_ERROR,   LOGGER_ANDROID_TAG, __VA_ARGS__)
// #else
#define LOGV(...) Logger::Get().verbose( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGD(...) Logger::Get().debug  ( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGI(...) Logger::Get().info   ( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGW(...) Logger::Get().warning( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
#define LOGE(...) Logger::Get().error  ( __FILE__, __FUNCTION__, __LINE__, __VA_ARGS__)
// #endif

// ----------------------------------------------------------------------------
// Special aliases.

#define LOG_FATAL(...)  LOGE(__VA_ARGS__); exit(-1)
#define LOG_LINE()      LOGD("{} {}", __FUNCTION__, __LINE__)
#define LOG_CHECK(x)    assert(x)

// ----------------------------------------------------------------------------
// Disable Debug log on release.

#if defined(NDEBUG)
#undef LOGD
#define LOGD(...)

#undef LOG_LINE
#define LOG_LINE()
#endif

// ----------------------------------------------------------------------------
// Undef verbose log when not asked for.

#if defined(NDEBUG) || !defined(VERBOSE_LOG)
#undef LOGV
#define LOGV(...)
#endif

/* -------------------------------------------------------------------------- */

#endif // AER_CORE_LOGGER_H