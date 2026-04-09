#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace linx::model {

/**
 * @brief A contiguous loaded region from an ELF image or raw binary.
 */
struct ProgramSection {
  std::string name;
  std::uint64_t address = 0;
  std::vector<std::uint8_t> bytes;
  bool executable = false;
};

/**
 * @brief Loaded program image used by `SimSystem` and ISA disassembly helpers.
 */
struct ProgramImage {
  std::string source_path;
  std::uint64_t entry_point = 0;
  std::vector<ProgramSection> sections;

  [[nodiscard]] bool Empty() const noexcept {
    return sections.empty();
  }

  [[nodiscard]] std::size_t ExecutableSectionCount() const noexcept;
  [[nodiscard]] std::span<const ProgramSection> Sections() const noexcept {
    return sections;
  }
};

} // namespace linx::model
