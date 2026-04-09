#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "linx/model/emulator/minst_record_c.h"
#include "linx/model/isa/minst.hpp"

namespace linx::model::emulator {

using MinstRecord = LinxMinstTraceRecordC;

struct MinstRecordDecoration {
  std::string pc_hex;
  std::string next_pc_hex;
  std::string insn_hex;
  std::string bytes_hex;
  std::string asm_text;
  std::string dump_line;
};

[[nodiscard]] std::string JsonEscape(std::string_view text);
[[nodiscard]] MinstRecordDecoration DecorateMinstRecord(const MinstRecord &record);
void WriteMinstRecordJson(std::ostream &os, const MinstRecord &record);
void WriteMinstRecordDump(std::ostream &os, const MinstRecord &record);
[[nodiscard]] bool EqualMinstRecord(const MinstRecord &lhs, const MinstRecord &rhs,
                                    std::string *why = nullptr);
[[nodiscard]] std::string DumpMinstRecord(const MinstRecord &record);
[[nodiscard]] MinstRecord MakeMinstRecord(const isa::Minst &inst, std::uint64_t cycle,
                                          std::string_view block_kind, int lane_id,
                                          std::optional<std::uint16_t> trap_cause = std::nullopt,
                                          std::optional<std::uint64_t> traparg0 = std::nullopt);

} // namespace linx::model::emulator
