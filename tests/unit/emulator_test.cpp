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
using linx::model::emulator::SharedTileBank;
using linx::model::emulator::SharedTileDescriptor;
using linx::model::emulator::SharedTileWriteStatus;
using linx::model::emulator::TileBindingKind;
using linx::model::emulator::TileOperandSpace;
using linx::model::emulator::TileOperationKind;
using linx::model::emulator::TileSharedUse;
using linx::model::emulator::WriteMinstRecordDump;
using linx::model::isa::DecodeMinstPacked;
using linx::model::isa::EncodeMinst;
using linx::model::isa::FieldsFor;
using linx::model::isa::LookupFormByUid;
using linx::model::isa::Minst;
using linx::model::isa::MinstCodecStatus;

Minst BuildZeroInst(std::string_view uid) {
  Minst inst;
  const auto *form = LookupFormByUid(uid);
  if (form == nullptr) {
    return inst;
  }
  inst.SetForm(form);
  for (const auto &field : FieldsFor(*form)) {
    inst.SetDecodedField(field.name, 0, field.signed_hint > 0, field.bit_width);
  }
  inst.RebuildTypedViews();
  return inst;
}

std::vector<std::uint8_t> EncodedBytes(const Minst &inst) {
  const auto encoded = EncodeMinst(inst);
  std::vector<std::uint8_t> bytes;
  if (!encoded.valid) {
    return bytes;
  }
  const auto byte_count = encoded.length_bits / 8U;
  bytes.reserve(byte_count);
  for (std::uint8_t idx = 0; idx < byte_count; ++idx) {
    bytes.push_back(static_cast<std::uint8_t>((encoded.bits >> (idx * 8U)) & 0xffU));
  }
  return bytes;
}

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

int TestMemoryRangeRead() {
  ExecutionContext ctx;
  ctx.Write32(0x20000, 0x646c6f67U);
  const auto bytes = ctx.ReadMemoryRange(0x20000, 4);
  if (!bytes.has_value() || *bytes != std::vector<std::uint8_t>({'g', 'o', 'l', 'd'}) ||
      ctx.ReadMemoryRange(0x20002, 4).has_value()) {
    return 44;
  }
  return 0;
}

int TestSharedTileState() {
  SharedTileBank bank;
  const SharedTileDescriptor descriptor{
      .dtype = 3,
      .valid_cols = 8,
      .valid_rows = 4,
      .cols = 8,
      .rows = 4,
  };

  for (std::uint8_t code = 1; code <= 7; ++code) {
    const auto bytes = SharedTileBank::CapacityBytes(code);
    if (!bytes.has_value() || *bytes != (128U << (code - 1U))) {
      return 29;
    }
  }
  if (SharedTileBank::CapacityBytes(0).has_value() ||
      SharedTileBank::CapacityBytes(8).has_value()) {
    return 30;
  }

  std::array<std::vector<std::uint8_t>, 4> payloads{};
  std::array<SharedTileDescriptor, 4> descriptors{};
  descriptors.fill(descriptor);
  descriptors[1].valid_rows = 2;
  payloads[0].assign(128, 0x11);
  payloads[1].assign(128, 0x22);
  if (bank.Write(7, 0, 1, descriptor, payloads) != SharedTileWriteStatus::Noop ||
      bank.Version(7).allocation_mask != 0 || bank.Version(7).initialized_mask != 0) {
    return 31;
  }
  if (bank.Write(7, 0xc, 1, descriptors, payloads) != SharedTileWriteStatus::Applied ||
      bank.Version(7).allocation_mask != 0xc || bank.Version(7).initialized_mask != 0xc ||
      bank.Version(7).allocated_bytes != 256 || bank.Read(7, 1)->descriptor.valid_rows != 2) {
    return 32;
  }
  if (bank.Read(7, 2) != nullptr || bank.Version(7).initialized_mask != 0xc) {
    return 33;
  }

  const auto lane1_before = bank.Read(7, 1)->data;
  payloads[0].assign(128, 0x44);
  if (bank.Write(7, 0x8, 1, descriptor, payloads) != SharedTileWriteStatus::Applied ||
      bank.Read(7, 0)->data.front() != 0x44 || bank.Read(7, 1)->data != lane1_before ||
      bank.Version(7).allocation_mask != 0xc) {
    return 34;
  }

  const auto lane0_before = bank.Read(7, 0)->data;
  payloads[0].assign(128, 0x66);
  payloads[1].assign(1, 0x77);
  if (bank.Write(7, 0xc, 1, descriptors, payloads) != SharedTileWriteStatus::PayloadSizeMismatch ||
      bank.Read(7, 0)->data != lane0_before || bank.Read(7, 1)->data != lane1_before) {
    return 35;
  }

  payloads[1].assign(128, 0x22);
  payloads[2].assign(128, 0x55);
  if (bank.Write(7, 0x2, 1, descriptor, payloads) != SharedTileWriteStatus::AllocationExpansion ||
      bank.Version(7).allocation_mask != 0xc || bank.Read(7, 2) != nullptr) {
    return 36;
  }
  auto mismatched = descriptor;
  mismatched.dtype = 9;
  if (bank.Write(7, 0x8, 1, mismatched, payloads) != SharedTileWriteStatus::DescriptorMismatch ||
      bank.Read(7, 0)->descriptor.dtype != 3 || bank.Read(7, 0)->data.front() != 0x44) {
    return 37;
  }

  bank.Reset();
  if (bank.Version(7).allocation_mask != 0 || bank.Read(7, 0) != nullptr) {
    return 38;
  }
  return 0;
}

int TestSharedTileBindingPolicy() {
  if (!linx::model::emulator::BindingAllows(TileBindingKind::Bior,
                                            TileOperandSpace::ScalarAddress) ||
      linx::model::emulator::BindingAllows(TileBindingKind::Bior, TileOperandSpace::Local) ||
      linx::model::emulator::BindingAllows(TileBindingKind::Bior, TileOperandSpace::Shared) ||
      !linx::model::emulator::BindingAllows(TileBindingKind::Biot, TileOperandSpace::Local) ||
      linx::model::emulator::BindingAllows(TileBindingKind::Biot, TileOperandSpace::Shared) ||
      !linx::model::emulator::BindingAllows(TileBindingKind::Bios, TileOperandSpace::Shared) ||
      linx::model::emulator::BindingAllows(TileBindingKind::Bios, TileOperandSpace::Local)) {
    return 38;
  }
  if (!linx::model::emulator::ValidateSharedOperation(TileOperationKind::Tmov,
                                                      TileSharedUse::Destination, 0x3, 0x3) ||
      linx::model::emulator::ValidateSharedOperation(TileOperationKind::Tmov,
                                                     TileSharedUse::Destination, 0x3, 0x1) ||
      !linx::model::emulator::ValidateSharedOperation(TileOperationKind::Cube,
                                                      TileSharedUse::Source, 0xf, 0xf) ||
      linx::model::emulator::ValidateSharedOperation(TileOperationKind::Cube,
                                                     TileSharedUse::Destination, 0xf, 0xf) ||
      linx::model::emulator::ValidateSharedOperation(TileOperationKind::Cube, TileSharedUse::Source,
                                                     0x7, 0x7) ||
      linx::model::emulator::ValidateSharedOperation(TileOperationKind::Tgemv,
                                                     TileSharedUse::Source, 0xf, 0xf)) {
    return 39;
  }
  return 0;
}

int TestSharedTileBindingDecode() {
  Minst inst;
  if (DecodeMinstPacked(0x00001013ULL, 32, inst) != MinstCodecStatus::Ok ||
      inst.mnemonic != "B.IOS" || inst.form_id != "4ba5ef98fdaa") {
    return 40;
  }
  const std::uint64_t boundary = 0x00001013ULL | (0xffULL << 20U) | (0xfULL << 15U) | (7ULL << 9U);
  Minst boundary_inst;
  if (DecodeMinstPacked(boundary, 32, boundary_inst) != MinstCodecStatus::Ok ||
      boundary_inst.mnemonic != "B.IOS" ||
      boundary_inst.GetFieldUnsigned("SharedTID").value_or(0) != 0xff ||
      boundary_inst.GetFieldUnsigned("PE_MASK").value_or(0) != 0xf ||
      boundary_inst.GetFieldUnsigned("TSize").value_or(0) != 7) {
    return 41;
  }
  Minst retired;
  if (DecodeMinstPacked(0xc03cULL, 16, retired) == MinstCodecStatus::Ok &&
      retired.mnemonic == "C.B.IOS") {
    return 42;
  }
  Minst reserved;
  if (DecodeMinstPacked(0x10001013ULL, 32, reserved) == MinstCodecStatus::Ok &&
      reserved.mnemonic == "B.IOS") {
    return 43;
  }
  return 0;
}

int TestTileHeadersAndUnsupportedScalar() {
  {
    const auto bytes = EncodedBytes(BuildZeroInst("d5f83e5aadf6")); // BSTART.TPREFETCH
    if (bytes.empty()) {
      return 23;
    }
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->LoadProgram(LoadRawBinaryImageFromBytes(bytes, "tprefetch-header", 0));
    ReferenceExecutor executor(ctx);
    if (!executor.Step() || ctx->Terminated() || ctx->State().block_kind != "tlsu" ||
        !ctx->LastCommitted().has_value() ||
        std::string_view(ctx->LastCommitted()->mnemonic) != "BSTART.TPREFETCH" ||
        std::string_view(ctx->LastCommitted()->block_kind) != "tlsu") {
      return 24;
    }
  }

  {
    const auto bytes = EncodedBytes(BuildZeroInst("ae19f5b678f5")); // BSTART.TGEMV
    if (bytes.empty()) {
      return 25;
    }
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->LoadProgram(LoadRawBinaryImageFromBytes(bytes, "tgemv-header", 0));
    ReferenceExecutor executor(ctx);
    if (!executor.Step() || ctx->Terminated() || ctx->State().block_kind != "cube" ||
        !ctx->LastCommitted().has_value() ||
        std::string_view(ctx->LastCommitted()->mnemonic) != "BSTART.TGEMV" ||
        std::string_view(ctx->LastCommitted()->block_kind) != "cube") {
      return 26;
    }
  }

  {
    const auto bytes = EncodedBytes(BuildZeroInst("7e529b871832")); // CASB
    if (bytes.empty()) {
      return 27;
    }
    auto ctx = std::make_shared<ExecutionContext>();
    ctx->LoadProgram(LoadRawBinaryImageFromBytes(bytes, "casb-unsupported", 0));
    ReferenceExecutor executor(ctx);
    if (executor.Step() || !ctx->Terminated() || ctx->ExitCode() != 1 ||
        ctx->LastError() != "unsupported_instruction:CASB" || !ctx->LastCommitted().has_value() ||
        std::string_view(ctx->LastCommitted()->mnemonic) != "CASB" ||
        std::string_view(ctx->LastCommitted()->opcode_class) != "atomic") {
      return 28;
    }
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
  if (const int rc = TestMemoryRangeRead(); rc != 0) {
    return rc;
  }
  if (const int rc = TestSharedTileState(); rc != 0) {
    return rc;
  }
  if (const int rc = TestSharedTileBindingPolicy(); rc != 0) {
    return rc;
  }
  if (const int rc = TestSharedTileBindingDecode(); rc != 0) {
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
  if (const int rc = TestTileHeadersAndUnsupportedScalar(); rc != 0) {
    return rc;
  }
  return 0;
}
