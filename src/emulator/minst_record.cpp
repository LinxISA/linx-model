#include "linx/model/emulator/minst_record.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "linx/model/isa/disasm.hpp"
#include "linx/model/sim_assert.hpp"

namespace linx::model::emulator {

namespace {

template <std::size_t N>
void CopyText(char (&dst)[N], std::string_view src) {
  std::memset(dst, 0, sizeof(dst));
  const auto count = std::min<std::size_t>(src.size(), N - 1U);
  std::memcpy(dst, src.data(), count);
}

void FillOperand(LinxMinstOperandRecordC &dst, const isa::MinstOperand *src) {
  std::memset(&dst, 0, sizeof(dst));
  if (src == nullptr) {
    return;
  }
  dst.valid = 1;
  dst.kind = static_cast<std::uint8_t>(src->kind);
  dst.is_dst = src->is_dst ? 1 : 0;
  dst.ready = src->ready ? 1 : 0;
  dst.encoded_bits = src->encoded_bits;
  dst.value = src->value;
  dst.data = src->data;
  CopyText(dst.field_name, src->field_name);
}

[[nodiscard]] std::string HexValue(std::uint64_t value, std::size_t digits, bool with_prefix) {
  std::ostringstream oss;
  if (with_prefix) {
    oss << "0x";
  }
  oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(static_cast<int>(digits))
      << value;
  return oss.str();
}

[[nodiscard]] std::size_t BytesForLen(std::uint32_t len_bits) {
  LINX_MODEL_ASSERT_MSG(len_bits % 8U == 0U, "minst trace len must be byte-aligned");
  LINX_MODEL_ASSERT_MSG(len_bits >= 16U && len_bits <= 64U, "minst trace len must stay within v0.4 fetch sizes");
  return static_cast<std::size_t>(len_bits / 8U);
}

[[nodiscard]] std::array<std::uint8_t, 8> UnpackBytes(const MinstRecord &record) {
  std::array<std::uint8_t, 8> bytes{};
  const auto size_bytes = BytesForLen(record.len);
  for (std::size_t idx = 0; idx < size_bytes; ++idx) {
    bytes[idx] = static_cast<std::uint8_t>((record.insn >> (idx * 8U)) & 0xffU);
  }
  return bytes;
}

[[nodiscard]] isa::MinstDisassemblyLine DecodeRecordLine(const MinstRecord &record) {
  const auto bytes = UnpackBytes(record);
  const auto size_bytes = BytesForLen(record.len);
  const auto line =
      isa::DecodeDisassemblyLine(std::span<const std::uint8_t>(bytes.data(), size_bytes), record.pc, "trace");
  LINX_MODEL_ASSERT_MSG(line.has_value(), "trace decode should always yield a printable line");
  return *line;
}

} // namespace

std::string JsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const char ch : text) {
    switch (ch) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out.push_back(ch);
      break;
    }
  }
  return out;
}

MinstRecordDecoration DecorateMinstRecord(const MinstRecord &record) {
  const auto line = DecodeRecordLine(record);
  MinstRecordDecoration out{
      .pc_hex = HexValue(record.pc, 16, true),
      .next_pc_hex = HexValue(record.next_pc, 16, true),
      .insn_hex = HexValue(record.insn, std::max<std::size_t>(record.len / 4U, 1U), true),
      .bytes_hex = line.bytes_hex,
      .asm_text = line.text,
  };
  out.dump_line = isa::FormatDisassemblyDumpLine(line);
  return out;
}

void WriteMinstRecordJson(std::ostream &os, const MinstRecord &record) {
  const auto decoration = DecorateMinstRecord(record);
  os << "{\"schema_version\":\"1.0\""
     << ",\"cycle\":" << record.cycle << ",\"pc\":" << record.pc << ",\"next_pc\":" << record.next_pc
     << ",\"insn\":" << record.insn << ",\"len\":" << record.len << ",\"lane_id\":" << record.lane_id
     << ",\"pc_hex\":\"" << JsonEscape(decoration.pc_hex) << "\",\"next_pc_hex\":\""
     << JsonEscape(decoration.next_pc_hex) << "\",\"insn_hex\":\"" << JsonEscape(decoration.insn_hex)
     << "\",\"bytes_hex\":\"" << JsonEscape(decoration.bytes_hex) << "\",\"asm\":\""
     << JsonEscape(decoration.asm_text) << "\",\"dump\":\"" << JsonEscape(decoration.dump_line) << "\""
     << ",\"mnemonic\":\"" << JsonEscape(record.mnemonic) << "\",\"form_id\":\""
     << JsonEscape(record.form_id) << "\",\"opcode_class\":\"" << JsonEscape(record.opcode_class)
     << "\",\"lifecycle\":\"" << JsonEscape(record.lifecycle) << "\",\"block_kind\":\""
     << JsonEscape(record.block_kind) << "\",\"src0_valid\":" << static_cast<unsigned>(record.src0.valid)
     << ",\"src0_kind\":" << static_cast<unsigned>(record.src0.kind) << ",\"src0_value\":" << record.src0.value
     << ",\"src0_data\":" << record.src0.data << ",\"src1_valid\":" << static_cast<unsigned>(record.src1.valid)
     << ",\"src1_kind\":" << static_cast<unsigned>(record.src1.kind) << ",\"src1_value\":" << record.src1.value
     << ",\"src1_data\":" << record.src1.data << ",\"dst0_valid\":" << static_cast<unsigned>(record.dst0.valid)
     << ",\"dst0_kind\":" << static_cast<unsigned>(record.dst0.kind) << ",\"dst0_value\":" << record.dst0.value
     << ",\"dst0_data\":" << record.dst0.data << ",\"mem_valid\":" << static_cast<unsigned>(record.memory.valid)
     << ",\"mem_is_load\":" << static_cast<unsigned>(record.memory.is_load)
     << ",\"mem_is_store\":" << static_cast<unsigned>(record.memory.is_store)
     << ",\"mem_addr\":" << record.memory.addr << ",\"mem_size\":" << record.memory.size
     << ",\"mem_wdata\":" << record.memory.wdata << ",\"mem_rdata\":" << record.memory.rdata
     << ",\"trap_valid\":" << static_cast<unsigned>(record.trap.valid) << ",\"trap_cause\":"
     << record.trap.cause << ",\"traparg0\":" << record.trap.traparg0 << "}";
}

void WriteMinstRecordDump(std::ostream &os, const MinstRecord &record) {
  os << DecorateMinstRecord(record).dump_line;
}

std::string DumpMinstRecord(const MinstRecord &record) {
  std::ostringstream oss;
  WriteMinstRecordJson(oss, record);
  return oss.str();
}

bool EqualMinstRecord(const MinstRecord &lhs, const MinstRecord &rhs, std::string *why) {
  if (std::memcmp(&lhs, &rhs, sizeof(MinstRecord)) == 0) {
    return true;
  }
  if (why != nullptr) {
    *why = "record mismatch: ref=" + DumpMinstRecord(lhs) + " ca=" + DumpMinstRecord(rhs);
  }
  return false;
}

MinstRecord MakeMinstRecord(const isa::Minst &inst, std::uint64_t cycle, std::string_view block_kind,
                            int lane_id, std::optional<std::uint16_t> trap_cause,
                            std::optional<std::uint64_t> traparg0) {
  MinstRecord record{};
  record.cycle = cycle;
  record.pc = inst.pc;
  record.next_pc = inst.next_pc;
  record.insn = inst.raw_bits;
  record.len = inst.length_bits;
  record.lane_id = lane_id;
  CopyText(record.mnemonic, inst.mnemonic);
  CopyText(record.form_id, inst.form_id);
  CopyText(record.opcode_class, isa::ToString(inst.opcode_class));
  CopyText(record.lifecycle, isa::ToString(inst.lifecycle));
  CopyText(record.block_kind, block_kind);
  if (!inst.srcs.empty()) {
    FillOperand(record.src0, &inst.srcs[0]);
  }
  if (inst.srcs.size() > 1U) {
    FillOperand(record.src1, &inst.srcs[1]);
  }
  if (!inst.dsts.empty()) {
    FillOperand(record.dst0, &inst.dsts[0]);
  }
  if (inst.memory.has_value()) {
    record.memory.valid = inst.memory->valid ? 1 : 0;
    record.memory.is_load = inst.memory->is_load ? 1 : 0;
    record.memory.is_store = inst.memory->is_store ? 1 : 0;
    record.memory.size = static_cast<std::uint32_t>(inst.memory->size);
    record.memory.addr = inst.memory->addr;
  }
  if (trap_cause.has_value()) {
    record.trap.valid = 1;
    record.trap.cause = *trap_cause;
    record.trap.traparg0 = traparg0.value_or(0);
  }
  return record;
}

} // namespace linx::model::emulator
