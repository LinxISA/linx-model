#pragma once

#include <string>
#include <vector>

namespace linx::model {

class SimSystem;

/**
 * @brief Severity assigned to validation findings.
 */
enum class ValidationSeverity {
  Warning,
  Error,
};

/**
 * @brief One validation finding emitted by the model checker.
 */
struct ValidationIssue {
  ValidationSeverity severity = ValidationSeverity::Error;
  std::string component;
  std::string message;
};

/**
 * @brief Accumulates validation issues for a built simulation model.
 */
class ValidationReport {
public:
  void AddError(std::string component, std::string message);
  void AddWarning(std::string component, std::string message);

  [[nodiscard]] bool Ok() const noexcept;
  [[nodiscard]] bool HasErrors() const noexcept;
  [[nodiscard]] const std::vector<ValidationIssue> &Issues() const noexcept;
  [[nodiscard]] std::string Format() const;

private:
  std::vector<ValidationIssue> issues_;
};

/**
 * @brief Validate a built simulation system against queue-based framework contracts.
 */
[[nodiscard]] ValidationReport ValidateModel(const SimSystem &sim);

} // namespace linx::model
