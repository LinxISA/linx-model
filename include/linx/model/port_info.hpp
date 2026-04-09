#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace linx::model {

/**
 * @brief Declared role for a queue port on a module.
 */
enum class PortDirection {
  Input,
  Output,
  Inner,
};

inline std::string_view ToString(PortDirection direction) noexcept {
  switch (direction) {
  case PortDirection::Input:
    return "input";
  case PortDirection::Output:
    return "output";
  case PortDirection::Inner:
    return "inner";
  }
  return "unknown";
}

struct PortInfo {
  /// Stable port index within its direction list.
  std::size_t index = 0;
  /// Whether the port is an input, output, or internal queue link.
  PortDirection direction = PortDirection::Input;
  /// Signal name shown in docs, validation, and logs.
  std::string name;
  /// Human-readable description of the signal semantics.
  std::string description;
};

} // namespace linx::model
