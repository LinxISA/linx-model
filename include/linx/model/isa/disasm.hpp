#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "linx/model/isa/minst.hpp"
#include "linx/model/program_image.hpp"

namespace linx::model::isa {

/**
 * @brief One disassembled instruction line from a loaded program image.
 */
struct MinstDisassemblyLine {
  std::string section_name;
  std::uint64_t pc = 0;
  std::uint64_t bits = 0;
  std::uint8_t length_bits = 0;
  std::size_t size_bytes = 0;
  MinstCodecStatus status = MinstCodecStatus::NoMatch;
  std::string bytes_hex;
  std::string text;
  std::string form_id;
};

[[nodiscard]] std::optional<MinstDisassemblyLine>
DecodeDisassemblyLine(std::span<const std::uint8_t> bytes, std::uint64_t pc,
                      std::string_view section_name);
[[nodiscard]] std::string FormatDisassemblyDumpLine(const MinstDisassemblyLine &line);
[[nodiscard]] std::vector<MinstDisassemblyLine> DisassembleSection(const ProgramSection &section);
[[nodiscard]] std::vector<MinstDisassemblyLine> DisassembleProgram(const ProgramImage &image);
void PrintDisassembly(std::ostream &os, const ProgramImage &image);

} // namespace linx::model::isa
