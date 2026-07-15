#include <array>
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
      0x0e, 0x00, 0x17, 0x51, 0x55, 0x05, // hl.lui 0x5555, ->a0
      0x0e, 0x10, 0x97, 0x0f, 0x00, 0x09, // hl.lui 0x10009000, ->t
      0x59, 0x20, 0x81, 0x01,             // swi a0, [t#1, 0]
      0x00, 0x00,                         // C.BSTOP
  };

  auto ctx = std::make_shared<ExecutionContext>();
  ctx->LoadProgram(LoadRawBinaryImageFromBytes(program, "unit", 0));
  ReferenceExecutor executor(ctx);
  executor.Run(std::nullopt, 16);

  if (!ctx->Terminated() || ctx->ExitCode() != 0 || ctx->LastError() != "finisher_pass") {
    return 4;
  }
  if (ctx->Committed().size() < 4U) {
    return 5;
  }
  return 0;
}

int TestReferenceExecutorImmediateContracts() {
  const std::vector<std::uint8_t> program = {
      0xfe, 0xff, 0x17, 0xf1, 0xff, 0xff, // hl.lui 0xffffffff, ->a0
      0x1e, 0x11, 0x97, 0x1f, 0x11, 0x11, // hl.lui 0x11111111, ->t
      0x2e, 0x22, 0x17, 0x2f, 0x22, 0x22, // hl.lui 0x22222222, ->u
      0x19, 0xa2, 0x11, 0x00,             // lwi [a1, 4], ->a2
      0xd9, 0xaf, 0x32, 0xfe,             // swi a3, [a1, -4]
  };

  auto ctx = std::make_shared<ExecutionContext>();
  ctx->LoadProgram(LoadRawBinaryImageFromBytes(program, "immediate-contracts", 0x1000));
  ctx->State().tq = {0x10, 0x11, 0x12, 0x13};
  ctx->State().uq = {0x20, 0x21, 0x22, 0x23};
  ctx->State().gpr[3] = UINT64_MAX - 3; // a1; +4 wraps to zero
  ctx->State().gpr[5] = 0x11223344;     // a3
  ctx->Write32(0, 0x89abcdef);

  ReferenceExecutor executor(ctx);
  if (!executor.Step() || ctx->State().gpr[2] != UINT64_MAX || !ctx->LastCommitted().has_value() ||
      ctx->LastCommitted()->dst0.data != UINT64_MAX) {
    return 18;
  }
  if (!executor.Step() ||
      ctx->State().tq != std::array<std::uint64_t, 4>{0x11111111, 0x10, 0x11, 0x12} ||
      ctx->State().uq != std::array<std::uint64_t, 4>{0x20, 0x21, 0x22, 0x23} ||
      !ctx->LastCommitted().has_value() || ctx->LastCommitted()->dst0.data != 0x11111111) {
    return 19;
  }
  if (!executor.Step() ||
      ctx->State().uq != std::array<std::uint64_t, 4>{0x22222222, 0x20, 0x21, 0x22} ||
      ctx->State().tq != std::array<std::uint64_t, 4>{0x11111111, 0x10, 0x11, 0x12} ||
      !ctx->LastCommitted().has_value() || ctx->LastCommitted()->dst0.data != 0x22222222) {
    return 20;
  }
  if (!executor.Step() || ctx->State().gpr[4] != 0xffffffff89abcdefULL ||
      !ctx->LastCommitted().has_value() || ctx->LastCommitted()->memory.addr != 0 ||
      ctx->LastCommitted()->memory.rdata != 0xffffffff89abcdefULL) {
    return 21;
  }
  ctx->State().gpr[3] = 2; // a1; -4 wraps to UINT64_MAX - 1
  if (!executor.Step() || ctx->Read32(UINT64_MAX - 1).value_or(0) != 0x11223344U ||
      !ctx->LastCommitted().has_value() || ctx->LastCommitted()->memory.addr != UINT64_MAX - 1) {
    return 22;
  }
  return 0;
}

int TestFinisherContract() {
  constexpr std::uint64_t kFinisher = 0x10009000ULL;
  constexpr std::uint64_t kLegacyExit = 0x10000004ULL;

  ExecutionContext ctx;
  ctx.Write32(kFinisher, 0x5555U);
  if (!ctx.Terminated() || ctx.ExitCode() != 0 || ctx.LastError() != "finisher_pass") {
    return 6;
  }

  ctx.Reset();
  ctx.Write32(kFinisher, (7U << 16U) | 0x3333U);
  if (!ctx.Terminated() || ctx.ExitCode() != 7 || ctx.LastError() != "finisher_fail") {
    return 7;
  }

  ctx.Reset();
  ctx.Write32(kFinisher, 0x7777U);
  if (!ctx.Terminated() || ctx.ExitCode() == 0 || ctx.LastError() != "finisher_reset") {
    return 8;
  }

  ctx.Reset();
  ctx.Write32(kFinisher, 0x1234U);
  if (ctx.Terminated()) {
    return 9;
  }

  ctx.Write32(kLegacyExit, 0U);
  ctx.Write64(kLegacyExit, 1U);
  if (ctx.Terminated()) {
    return 10;
  }
  return 0;
}

int TestCompareHarness() {
  Minst inst;
  if (DecodeMinstPacked(0x00000115ULL, 32, inst) != MinstCodecStatus::Ok) {
    return 11;
  }
  inst.MarkRetired();
  const auto a = MakeMinstRecord(inst, 0, "scalar", -1);
  auto b = a;
  CompareHarness harness(4);
  static const auto kZeroState = std::make_shared<const linx::model::emulator::LinxState>();
  if (!harness.Push(a, b, *kZeroState, *kZeroState)) {
    return 12;
  }
  b.next_pc += 4;
  if (harness.Push(a, b, *kZeroState, *kZeroState)) {
    return 13;
  }
  if (!harness.Mismatch().has_value()) {
    return 14;
  }
  return 0;
}

int TestMinstRecordDumpFormatting() {
  Minst inst;
  if (DecodeMinstPacked(0x00000115ULL, 32, inst) != MinstCodecStatus::Ok) {
    return 15;
  }
  inst.pc = 0x24;
  inst.next_pc = 0x28;
  inst.MarkRetired();
  const auto record = MakeMinstRecord(inst, 7, "scalar", -1);

  const auto json = DumpMinstRecord(record);
  if (json.find("\"pc_hex\":\"0x0000000000000024\"") == std::string::npos ||
      json.find("\"asm\":\"addi zero, 0, ->{t, u, a0}") == std::string::npos ||
      json.find("\"dump\":\"0000000000000024:") == std::string::npos) {
    return 16;
  }

  std::ostringstream dump;
  WriteMinstRecordDump(dump, record);
  const auto text = dump.str();
  if (text.find("0000000000000024:") == std::string::npos ||
      text.find("addi zero, 0, ->{t, u, a0}") == std::string::npos) {
    return 17;
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
  if (const int rc = TestReferenceExecutorImmediateContracts(); rc != 0) {
    return rc;
  }
  if (const int rc = TestFinisherContract(); rc != 0) {
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
