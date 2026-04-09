#include "linx/model/logging.hpp"

#include <iomanip>
#include <iostream>

namespace linx::model {

std::string_view ToString(LogLevel level) noexcept {
  switch (level) {
  case LogLevel::Trace:
    return "TRACE";
  case LogLevel::Debug:
    return "DEBUG";
  case LogLevel::Info:
    return "INFO";
  case LogLevel::Warning:
    return "WARN";
  case LogLevel::Error:
    return "ERROR";
  case LogLevel::Fatal:
    return "FATAL";
  }
  return "UNKNOWN";
}

SimLogger::SimLogger(std::ostream &sink) : sink_(&sink) {}

void SimLogger::SetMinLevel(LogLevel level) noexcept {
  min_level_ = level;
}

LogLevel SimLogger::MinLevel() const noexcept {
  return min_level_;
}

bool SimLogger::ShouldLog(LogLevel level) const noexcept {
  return level >= min_level_;
}

void SimLogger::SetSink(std::ostream &sink) noexcept {
  sink_ = &sink;
}

std::ostream &SimLogger::Sink() const noexcept {
  return *sink_;
}

void SimLogger::Emit(LogLevel level, const LogContext &context, std::string_view message) {
  if (!ShouldLog(level)) {
    return;
  }

  std::lock_guard<std::mutex> lock(mu_);
  (*sink_) << '[' << std::left << std::setw(5) << ToString(level) << "] cycle=";
  if (context.cycle.has_value()) {
    (*sink_) << *context.cycle;
  } else {
    (*sink_) << '?';
  }
  (*sink_) << " module=" << context.module << " stage=" << context.stage << " | " << message
           << '\n';
  if (level >= LogLevel::Error) {
    (*sink_) << "  at " << context.location.file_name() << ':' << context.location.line() << '\n';
  }
  sink_->flush();
}

SimLogger &DefaultLogger() {
  static SimLogger logger(std::clog);
  return logger;
}

LogLine::LogLine(SimLogger *logger, LogLevel level, LogContext context)
    : logger_(logger), level_(level), context_(std::move(context)) {}

LogLine::LogLine(LogLine &&other) noexcept
    : logger_(other.logger_), level_(other.level_), context_(other.context_),
      stream_(std::move(other.stream_)) {
  other.logger_ = nullptr;
}

LogLine::~LogLine() {
  if (logger_ != nullptr) {
    logger_->Emit(level_, context_, stream_.str());
  }
}

std::optional<LogLevel> ParseLogLevel(std::string_view text) {
  if (text == "trace") {
    return LogLevel::Trace;
  }
  if (text == "debug") {
    return LogLevel::Debug;
  }
  if (text == "info") {
    return LogLevel::Info;
  }
  if (text == "warn" || text == "warning") {
    return LogLevel::Warning;
  }
  if (text == "error") {
    return LogLevel::Error;
  }
  if (text == "fatal") {
    return LogLevel::Fatal;
  }
  return std::nullopt;
}

} // namespace linx::model
