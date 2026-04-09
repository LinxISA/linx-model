#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <ostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>

#include "linx/model/packet_dump.hpp"

namespace linx::model {

/**
 * @brief Severity for structured simulator logs.
 */
enum class LogLevel : std::uint8_t {
  Trace = 0,
  Debug = 1,
  Info = 2,
  Warning = 3,
  Error = 4,
  Fatal = 5,
};

[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;

/**
 * @brief Runtime fields attached to every log line.
 */
struct LogContext {
  std::optional<std::uint64_t> cycle;
  std::string_view module = "unnamed";
  std::string_view stage = "-";
  std::source_location location = std::source_location::current();
};

/**
 * @brief Thread-safe sink for structured model logs.
 */
class SimLogger {
public:
  explicit SimLogger(std::ostream &sink);

  void SetMinLevel(LogLevel level) noexcept;
  [[nodiscard]] LogLevel MinLevel() const noexcept;
  [[nodiscard]] bool ShouldLog(LogLevel level) const noexcept;

  void SetSink(std::ostream &sink) noexcept;
  [[nodiscard]] std::ostream &Sink() const noexcept;

  void Emit(LogLevel level, const LogContext &context, std::string_view message);

private:
  std::ostream *sink_ = nullptr;
  LogLevel min_level_ = LogLevel::Info;
  mutable std::mutex mu_;
};

[[nodiscard]] SimLogger &DefaultLogger();

/**
 * @brief RAII stream helper used by `SimObject::Log()`.
 */
class LogLine {
public:
  LogLine(SimLogger *logger, LogLevel level, LogContext context);
  LogLine(LogLine &&other) noexcept;
  LogLine(const LogLine &) = delete;
  LogLine &operator=(const LogLine &) = delete;
  LogLine &operator=(LogLine &&) = delete;
  ~LogLine();

  template <class T> LogLine &operator<<(const T &value) {
    DumpValue(stream_, value);
    return *this;
  }

  LogLine &operator<<(std::ostream &(*manip)(std::ostream &)) {
    manip(stream_);
    return *this;
  }

  LogLine &operator<<(std::ios_base &(*manip)(std::ios_base &)) {
    manip(stream_);
    return *this;
  }

private:
  SimLogger *logger_ = nullptr;
  LogLevel level_ = LogLevel::Info;
  LogContext context_;
  std::ostringstream stream_;
};

[[nodiscard]] std::optional<LogLevel> ParseLogLevel(std::string_view text);

} // namespace linx::model

#define LOG_TRACE(stage) this->Log(::linx::model::LogLevel::Trace, (stage))
#define LOG_DEBUG(stage) this->Log(::linx::model::LogLevel::Debug, (stage))
#define LOG_INFO(stage) this->Log(::linx::model::LogLevel::Info, (stage))
#define LOG_WARN(stage) this->Log(::linx::model::LogLevel::Warning, (stage))
#define LOG_ERROR(stage) this->Log(::linx::model::LogLevel::Error, (stage))
#define LOG_FATAL(stage) this->Log(::linx::model::LogLevel::Fatal, (stage))
