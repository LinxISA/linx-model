#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "linx/model.hpp"

namespace {

using linx::model::LoadRawBinaryImageFromBytes;
using linx::model::emulator::CompareHarness;
using linx::model::emulator::DumpMinstRecord;
using linx::model::emulator::ExecutionContext;
using linx::model::emulator::MakeMinstRecord;
using linx::model::emulator::ReferenceExecutor;
using linx::model::emulator::WriteMinstRecordDump;
using linx::model::isa::DecodeMinstPacked;
using linx::model::isa::Minst;
using linx::model::isa::MinstCodecStatus;

int TestStateReset() {
  auto state = std::make_shared<linx::model::emulator::LinxState>();
  state->gpr[2] = 9;
  state->block_kind = "vpar";
  state->Reset();
  if (state->gpr[2] != 0 || state->block_kind != "scalar" || state->lane_id != -1) {
    return 1;
  }
  return 0;
}

int TestMinstRecordAdapter() {
  Minst inst;
  if (DecodeMinstPacked(0x00000115ULL, 32, inst) != MinstCodecStatus::Ok) {
    return 2;
  }
  inst.MarkRetired();
  const auto record = MakeMinstRecord(inst, 3, "scalar", -1);
  if (record.pc != 0 || std::string_view(record.mnemonic) != "ADDI" ||
      std::string_view(record.lifecycle) != "retired") {
    return 3;
  }
  return 0;
}

int TestReferenceExecutorExit() {
  const std::vector<std::uint8_t> program = {
      0x00, 0x08,                         // C.BSTART.STD
      0x15, 0x01, 0x00, 0x00,             // addi zero, 0, ->a0
      0x0e, 0x10, 0x97, 0x4f, 0x00, 0x00, // hl.lui 0x10000004, ->t
      0x59, 0x20, 0x81, 0x01,             // swi a0, [t#1, 0]
      0x00, 0x00,                         // C.BSTOP
  };

  auto ctx = std::make_shared<ExecutionContext>();
  ctx->LoadProgram(LoadRawBinaryImageFromBytes(program, "unit", 0));
  ReferenceExecutor executor(ctx);
  executor.Run(std::nullopt, 16);

  if (!ctx->Terminated() || ctx->ExitCode() != 0) {
    return 4;
  }
  if (ctx->Committed().size() < 4U) {
    return 5;
  }
  return 0;
}

int TestCompareHarness() {
  Minst inst;
  if (DecodeMinstPacked(0x00000115ULL, 32, inst) != MinstCodecStatus::Ok) {
    return 6;
  }
  inst.MarkRetired();
  const auto a = MakeMinstRecord(inst, 0, "scalar", -1);
  auto b = a;
  CompareHarness harness(4);
  static const auto kZeroState = std::make_shared<const linx::model::emulator::LinxState>();
  if (!harness.Push(a, b, *kZeroState, *kZeroState)) {
    return 7;
  }
  b.next_pc += 4;
  if (harness.Push(a, b, *kZeroState, *kZeroState)) {
    return 8;
  }
  if (!harness.Mismatch().has_value()) {
    return 9;
  }
  return 0;
}

int TestMinstRecordDumpFormatting() {
  Minst inst;
  if (DecodeMinstPacked(0x00000115ULL, 32, inst) != MinstCodecStatus::Ok) {
    return 10;
  }
  inst.pc = 0x24;
  inst.next_pc = 0x28;
  inst.MarkRetired();
  const auto record = MakeMinstRecord(inst, 7, "scalar", -1);

  const auto json = DumpMinstRecord(record);
  if (json.find("\"pc_hex\":\"0x0000000000000024\"") == std::string::npos ||
      json.find("\"asm\":\"addi zero, 0, ->{t, u, a0}") == std::string::npos ||
      json.find("\"dump\":\"0000000000000024:") == std::string::npos) {
    return 11;
  }

  std::ostringstream dump;
  WriteMinstRecordDump(dump, record);
  const auto text = dump.str();
  if (text.find("0000000000000024:") == std::string::npos ||
      text.find("addi zero, 0, ->{t, u, a0}") == std::string::npos) {
    return 12;
  }
  return 0;
}

} // namespace

int main() {
  if (const int rc = TestStateReset(); rc != 0) {
    return rc;
  }
  if (const int rc = TestMinstRecordAdapter(); rc != 0) {
    return rc;
  }
  if (const int rc = TestReferenceExecutorExit(); rc != 0) {
    return rc;
  }
  if (const int rc = TestCompareHarness(); rc != 0) {
    return rc;
  }
  if (const int rc = TestMinstRecordDumpFormatting(); rc != 0) {
    return rc;
  }
  return 0;
}
