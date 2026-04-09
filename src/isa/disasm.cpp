#include "linx/model/isa/disasm.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

#include "linx/model/isa/codec.hpp"

namespace linx::model::isa {

namespace {

constexpr std::array<int, 4> kCandidateLengths = {16, 32, 48, 64};

[[nodiscard]] std::uint64_t PackLittleEndian(std::span<const std::uint8_t> bytes,
                                             std::size_t size_bytes) {
  std::uint64_t value = 0;
  for (std::size_t idx = 0; idx < size_bytes; ++idx) {
    value |= static_cast<std::uint64_t>(bytes[idx]) << (idx * 8U);
  }
  return value;
}

[[nodiscard]] std::string HexBytes(std::span<const std::uint8_t> bytes) {
  std::ostringstream oss;
  for (std::size_t idx = 0; idx < bytes.size(); ++idx) {
    if (idx != 0U) {
      oss << ' ';
    }
    oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
        << static_cast<unsigned>(bytes[idx]);
  }
  return oss.str();
}

struct DecodeCandidate {
  Minst inst;
  int length_bits = 0;
  std::uint64_t bits = 0;
};

[[nodiscard]] std::optional<DecodeCandidate> DecodeBest(std::span<const std::uint8_t> bytes) {
  std::optional<DecodeCandidate> best;
  int best_fixed_bits = std::numeric_limits<int>::min();

  for (const auto length_bits : kCandidateLengths) {
    const auto size_bytes = static_cast<std::size_t>(length_bits / 8);
    if (bytes.size() < size_bytes) {
      continue;
    }

    DecodeCandidate candidate;
    candidate.length_bits = length_bits;
    candidate.bits = PackLittleEndian(bytes, size_bytes);
    if (DecodeMinstPacked(candidate.bits, length_bits, candidate.inst) != MinstCodecStatus::Ok) {
      continue;
    }

    const int fixed_bits =
        candidate.inst.form == nullptr ? 0 : static_cast<int>(candidate.inst.form->fixed_bits);
    if (!best.has_value() || fixed_bits > best_fixed_bits ||
        (fixed_bits == best_fixed_bits && length_bits < best->length_bits)) {
      best = std::move(candidate);
      best_fixed_bits = fixed_bits;
    }
  }

  return best;
}

} // namespace

std::string FormatDisassemblyDumpLine(const MinstDisassemblyLine &line) {
  std::ostringstream oss;
  oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(16) << line.pc << std::dec
      << ":  " << std::setfill(' ') << std::left << std::setw(23) << line.bytes_hex << std::right
      << ' ' << line.text;
  return oss.str();
}

std::optional<MinstDisassemblyLine> DecodeDisassemblyLine(std::span<const std::uint8_t> bytes,
                                                          std::uint64_t pc,
                                                          std::string_view section_name) {
  const auto decoded = DecodeBest(bytes);
  if (!decoded.has_value()) {
    if (bytes.empty()) {
      return std::nullopt;
    }
    return MinstDisassemblyLine{
        .section_name = std::string(section_name),
        .pc = pc,
        .bits = bytes.front(),
        .length_bits = 8,
        .size_bytes = 1,
        .status = MinstCodecStatus::NoMatch,
        .bytes_hex = HexBytes(bytes.first(1)),
        .text = ".byte 0x" +
                [&]() {
                  std::ostringstream oss;
                  oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(2)
                      << static_cast<unsigned>(bytes.front());
                  return oss.str();
                }(),
        .form_id = "-",
    };
  }

  const auto size_bytes = static_cast<std::size_t>(decoded->length_bits / 8);
  return MinstDisassemblyLine{
      .section_name = std::string(section_name),
      .pc = pc,
      .bits = decoded->bits,
      .length_bits = static_cast<std::uint8_t>(decoded->length_bits),
      .size_bytes = size_bytes,
      .status = decoded->inst.decode_status,
      .bytes_hex = HexBytes(bytes.first(size_bytes)),
      .text = decoded->inst.Assemble(),
      .form_id =
          decoded->inst.form_id.empty() ? std::string("-") : std::string(decoded->inst.form_id),
  };
}

std::vector<MinstDisassemblyLine> DisassembleSection(const ProgramSection &section) {
  std::vector<MinstDisassemblyLine> lines;
  if (!section.executable) {
    return lines;
  }

  std::size_t offset = 0;
  while (offset < section.bytes.size()) {
    const auto line =
        DecodeDisassemblyLine(std::span<const std::uint8_t>(section.bytes).subspan(offset),
                              section.address + offset, section.name);
    if (!line.has_value()) {
      break;
    }
    offset += std::max<std::size_t>(line->size_bytes, 1U);
    lines.push_back(*line);
  }

  return lines;
}

std::vector<MinstDisassemblyLine> DisassembleProgram(const ProgramImage &image) {
  std::vector<MinstDisassemblyLine> lines;
  for (const auto &section : image.sections) {
    if (!section.executable) {
      continue;
    }
    auto section_lines = DisassembleSection(section);
    lines.insert(lines.end(), std::make_move_iterator(section_lines.begin()),
                 std::make_move_iterator(section_lines.end()));
  }
  return lines;
}

void PrintDisassembly(std::ostream &os, const ProgramImage &image) {
  os << "source: " << image.source_path << '\n';
  os << "entry: 0x" << std::hex << image.entry_point << std::dec << '\n';

  const auto lines = DisassembleProgram(image);
  for (const auto &line : lines) {
    os << line.section_name << ' ';
    os << "0x" << std::hex << line.pc << std::dec;
    os << " [" << line.bytes_hex << "] ";
    os << line.text;
    if (line.status == MinstCodecStatus::Ok && line.form_id != "-") {
      os << " ; form=" << line.form_id;
    }
    os << '\n';
  }
}

} // namespace linx::model::isa
