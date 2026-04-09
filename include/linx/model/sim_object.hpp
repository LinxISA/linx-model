#pragma once

#include <cstdint>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>

#include "linx/model/logging.hpp"

namespace linx::model {

class SimSystem;
class ValidationReport;

/**
 * @brief Common phase hooks and runtime context for cycle-accurate components.
 */
class SimObject {
public:
  explicit SimObject(std::string module_name = "unnamed") : module_name_(std::move(module_name)) {}
  virtual ~SimObject() = default;

  virtual void Build() {}
  virtual void Reset() {}
  virtual void Report() {}
  virtual void Work() {}
  virtual void Xfer() {}
  virtual void CollectValidationIssues(ValidationReport &) const {}
  [[nodiscard]] virtual bool IsQueueBased() const noexcept {
    return false;
  }

  [[nodiscard]] std::string_view ModuleName() const noexcept {
    return module_name_;
  }
  void SetModuleName(std::string module_name) {
    module_name_ = std::move(module_name);
  }

  [[nodiscard]] std::optional<std::uint64_t> CurrentCycle() const noexcept {
    if (cycle_ptr_ == nullptr) {
      return std::nullopt;
    }
    return *cycle_ptr_;
  }

  [[nodiscard]] LogLine Log(LogLevel level, std::string_view stage = "-",
                            std::source_location location = std::source_location::current()) const {
    SimLogger &logger = logger_ != nullptr ? *logger_ : DefaultLogger();
    return LogLine(&logger, level,
                   LogContext{
                       .cycle = CurrentCycle(),
                       .module = module_name_,
                       .stage = stage,
                       .location = location,
                   });
  }

  void AttachRuntime(SimLogger *logger, const std::uint64_t *cycle_ptr) noexcept {
    logger_ = logger;
    cycle_ptr_ = cycle_ptr;
  }

protected:
  [[nodiscard]] SimLogger *RuntimeLogger() const noexcept {
    return logger_;
  }
  [[nodiscard]] const std::uint64_t *RuntimeCyclePtr() const noexcept {
    return cycle_ptr_;
  }

private:
  std::string module_name_;
  SimLogger *logger_ = nullptr;
  const std::uint64_t *cycle_ptr_ = nullptr;
};

} // namespace linx::model
