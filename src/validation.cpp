#include "linx/model/validation.hpp"

#include <sstream>

#include "linx/model/sim_system.hpp"

namespace linx::model {

void ValidationReport::AddError(std::string component, std::string message) {
  issues_.push_back(ValidationIssue{
      .severity = ValidationSeverity::Error,
      .component = std::move(component),
      .message = std::move(message),
  });
}

void ValidationReport::AddWarning(std::string component, std::string message) {
  issues_.push_back(ValidationIssue{
      .severity = ValidationSeverity::Warning,
      .component = std::move(component),
      .message = std::move(message),
  });
}

bool ValidationReport::Ok() const noexcept {
  return !HasErrors();
}

bool ValidationReport::HasErrors() const noexcept {
  for (const auto &issue : issues_) {
    if (issue.severity == ValidationSeverity::Error) {
      return true;
    }
  }
  return false;
}

const std::vector<ValidationIssue> &ValidationReport::Issues() const noexcept {
  return issues_;
}

std::string ValidationReport::Format() const {
  std::ostringstream oss;
  for (const auto &issue : issues_) {
    oss << (issue.severity == ValidationSeverity::Error ? "error" : "warning") << ": "
        << issue.component << ": " << issue.message << '\n';
  }
  return oss.str();
}

ValidationReport ValidateModel(const SimSystem &sim) {
  ValidationReport report;
  for (const auto *module : sim.Modules()) {
    if (module == nullptr) {
      report.AddError("sim", "registered null module");
      continue;
    }
    if (!module->IsQueueBased()) {
      report.AddError(std::string(module->ModuleName()),
                      "registered top-level object is not declared as SimQueue-based");
    }
    module->CollectValidationIssues(report);
  }
  return report;
}

ValidationReport SimSystem::Validate() const {
  return ValidateModel(*this);
}

} // namespace linx::model
