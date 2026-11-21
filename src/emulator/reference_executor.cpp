#include "linx/model/emulator/reference_executor.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <string>

namespace linx::model::emulator {

namespace {

std::optional<std::uint64_t> GetUnsignedAny(const isa::Minst &inst,
                                            std::initializer_list<std::string_view> names) {
  for (const auto name : names) {
    if (const auto value = inst.GetFieldUnsigned(name); value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> GetSignedAny(const isa::Minst &inst,
                                         std::initializer_list<std::string_view> names) {
  for (const auto name : names) {
    if (const auto value = inst.GetFieldSigned(name); value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

std::uint64_t ReadGpr(const LinxState &state, std::uint64_t idx) {
  if (idx == 0 || idx >= kLinxGprCount) {
    if (idx >= 24 && idx < 28) {
      return state.tq[idx - 24];
    }
    if (idx >= 28 && idx < 32) {
      return state.uq[idx - 28];
    }
    return 0;
  }
  return state.gpr[idx];
}

void PushQueue(std::array<std::uint64_t, kLinxQueueCount> &queue, std::uint64_t value) {
  for (std::size_t idx = queue.size() - 1; idx > 0; --idx) {
    queue[idx] = queue[idx - 1];
  }
  queue[0] = value;
}

void WriteScalarDestination(LinxState &state, std::uint64_t idx, std::uint64_t value) {
  if (idx == 31) {
    PushQueue(state.tq, value);
  } else if (idx == 30) {
    PushQueue(state.uq, value);
  } else if (idx > 0 && idx < kLinxGprCount) {
    state.gpr[idx] = value;
  }
}

std::string ResolveBlockKind(const isa::Minst &inst, const LinxState &state) {
  if (inst.mnemonic == "C.BSTART.VPAR") {
    return "vpar";
  }
  if (inst.mnemonic == "C.BSTART.VSEQ") {
    return "vseq";
  }
  if (inst.mnemonic == "BSTART.TEPL") {
    return "tepl";
  }
  if (inst.mnemonic == "BSTART.TMATMUL" || inst.mnemonic == "BSTART.TMATMUL.ACC" ||
      inst.mnemonic == "BSTART.TMATMUL.BIAS" || inst.mnemonic == "BSTART.TMATMULMX" ||
      inst.mnemonic == "BSTART.TMATMULMX.ACC" || inst.mnemonic == "BSTART.TMATMULMX.BIAS" ||
      inst.mnemonic == "BSTART.TGEMV" || inst.mnemonic == "BSTART.TGEMV.ACC" ||
      inst.mnemonic == "BSTART.TGEMV.BIAS" || inst.mnemonic == "BSTART.TGEMVMX" ||
      inst.mnemonic == "BSTART.TGEMVMX.ACC" || inst.mnemonic == "BSTART.TGEMVMX.BIAS") {
    return "cube";
  }
  if (inst.mnemonic == "BSTART.TLOAD" || inst.mnemonic == "BSTART.TSTORE" ||
      inst.mnemonic == "BSTART.TMOV" || inst.mnemonic == "BSTART.TPREFETCH" ||
      inst.mnemonic == "BSTART.MGATHER" || inst.mnemonic == "BSTART.MSCATTER" ||
      inst.mnemonic == "BSTART.MGATHER.MASK" || inst.mnemonic == "BSTART.MSCATTER.MASK" ||
      inst.mnemonic == "BSTART.MGATHER.CAS" || inst.mnemonic == "BSTART.GMOV") {
    return "tlsu";
  }
  if (inst.mnemonic == "SSRSET" || inst.mnemonic == "HL.SSRSET") {
    return "sys";
  }
  return state.block_kind;
}

int ResolveLaneId(std::string_view block_kind) {
  return (block_kind == "vpar" || block_kind == "vseq") ? 0 : -1;
}

bool IsTileHeader(const isa::Minst &inst) {
  const auto block_kind = ResolveBlockKind(inst, LinxState{});
  return block_kind == "tlsu" || block_kind == "cube" || block_kind == "tepl";
}

bool IsImplemented(const isa::Minst &inst) {
  return inst.mnemonic == "C.BSTART.STD" || inst.mnemonic == "C.BSTART.VPAR" ||
         inst.mnemonic == "C.BSTART.VSEQ" || inst.mnemonic == "C.BSTART" ||
         inst.mnemonic == "C.BSTOP" || inst.mnemonic == "C.MOVI" || inst.mnemonic == "C.MOVR" ||
         inst.mnemonic == "LUI" || inst.mnemonic == "HL.LUI" || inst.mnemonic == "HL.LIU" ||
         inst.mnemonic == "HL.LIS" || inst.mnemonic == "ADDI" || inst.mnemonic == "LWI" ||
         inst.mnemonic == "SWI" || inst.mnemonic == "SDI" || inst.mnemonic == "SETC.NE" ||
         inst.mnemonic == "SETC.LTU" || inst.mnemonic == "SSRSET" || inst.mnemonic == "MCOPY" ||
         inst.mnemonic == "MSET" || IsTileHeader(inst) || inst.mnemonic == "B.TEXT";
}

enum class ScopedRegisterField : std::uint8_t { None, RegDst, SrcL, SrcR, SrcD };
enum class ScopedImmediateField : std::uint8_t { Simm, Simm12, Simm17, Simm22 };

struct ScopedSingleAccessForm {
  std::string_view uid;
  bool is_load;
  bool is_pcr;
  std::uint8_t offset_scale;
  std::uint8_t size;
  bool signed_load;
  ScopedRegisterField base_field;
  ScopedRegisterField value_field;
  ScopedImmediateField immediate_field;
};

#include "pto_scoped_scalar_runtime.inc"
static_assert(std::size(kScopedSingleAccessForms) == 60);

const ScopedSingleAccessForm *LookupScopedSingleAccess(std::string_view uid) {
  const auto it = std::lower_bound(
      std::begin(kScopedSingleAccessForms), std::end(kScopedSingleAccessForms), uid,
      [](const ScopedSingleAccessForm &form, std::string_view key) { return form.uid < key; });
  return it != std::end(kScopedSingleAccessForms) && it->uid == uid ? &*it : nullptr;
}

std::string_view FieldName(ScopedRegisterField field) {
  switch (field) {
  case ScopedRegisterField::RegDst:
    return "RegDst";
  case ScopedRegisterField::SrcL:
    return "SrcL";
  case ScopedRegisterField::SrcR:
    return "SrcR";
  case ScopedRegisterField::SrcD:
    return "SrcD";
  case ScopedRegisterField::None:
    return {};
  }
  return {};
}

std::string_view FieldName(ScopedImmediateField field) {
  switch (field) {
  case ScopedImmediateField::Simm:
    return "simm";
  case ScopedImmediateField::Simm12:
    return "simm12";
  case ScopedImmediateField::Simm17:
    return "simm17";
  case ScopedImmediateField::Simm22:
    return "simm22";
  }
  return {};
}

std::optional<std::uint64_t> ReadMemoryValue(const ExecutionContext &ctx, std::uint64_t address,
                                             std::uint8_t size) {
  switch (size) {
  case 1:
    return ctx.Read8(address);
  case 2:
    return ctx.Read16(address);
  case 4:
    return ctx.Read32(address);
  case 8:
    return ctx.Read64(address);
  default:
    return std::nullopt;
  }
}

void WriteMemoryValue(ExecutionContext &ctx, std::uint64_t address, std::uint8_t size,
                      std::uint64_t value) {
  switch (size) {
  case 1:
    ctx.Write8(address, static_cast<std::uint8_t>(value));
    break;
  case 2:
    ctx.Write8(address, static_cast<std::uint8_t>(value));
    ctx.Write8(address + 1U, static_cast<std::uint8_t>(value >> 8U));
    break;
  case 4:
    ctx.Write32(address, static_cast<std::uint32_t>(value));
    break;
  case 8:
    ctx.Write64(address, value);
    break;
  default:
    LINX_MODEL_ASSERT_MSG(false, "invalid scoped Scalar access size");
  }
}

std::uint64_t NormalizeLoad(std::uint64_t value, std::uint8_t size, bool signed_load) {
  if (!signed_load) {
    switch (size) {
    case 1:
      return static_cast<std::uint8_t>(value);
    case 2:
      return static_cast<std::uint16_t>(value);
    case 4:
      return static_cast<std::uint32_t>(value);
    default:
      return value;
    }
  }
  switch (size) {
  case 1:
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int8_t>(value)));
  case 2:
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int16_t>(value)));
  case 4:
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(value)));
  default:
    return value;
  }
}

} // namespace

std::optional<isa::Minst>
ReferenceExecutor::FetchDecode(std::optional<isa::PtoScalarShareability> &shareability) const {
  const auto &ctx = *ctx_;
  const auto &state = ctx.State();
  const auto hw = ctx.Read16(state.pc);
  if (!hw.has_value()) {
    return std::nullopt;
  }

  const unsigned len = ((*hw & 0x1U) == 0U) ? (((*hw & 0xfU) == 0xeU) ? 6U : 2U)
                                            : (((*hw & 0xfU) == 0xfU) ? 8U : 4U);
  std::uint64_t packed = 0;
  for (unsigned idx = 0; idx < len; ++idx) {
    const auto byte = ctx.Read8(state.pc + idx);
    if (!byte.has_value()) {
      return std::nullopt;
    }
    packed |= static_cast<std::uint64_t>(*byte) << (idx * 8U);
  }

  shareability.reset();
  if (len == isa::PtoScopedScalar::CodeBytes && options_.enable_pto_scoped_scalar) {
    isa::PtoScopedScalar scoped;
    if (isa::DecodePtoScopedScalar(packed, scoped) == isa::MinstCodecStatus::Ok) {
      scoped.inner.uid = state.insn_count + 1U;
      scoped.inner.fetch_seq = state.insn_count + 1U;
      scoped.inner.pc = state.pc;
      scoped.inner.next_pc = state.pc + isa::PtoScopedScalar::CodeBytes;
      scoped.inner.raw_bits = packed;
      scoped.inner.length_bits = 64;
      scoped.inner.code_bytes = isa::PtoScopedScalar::CodeBytes;
      scoped.inner.MarkStage(isa::MinstStage::Decode);
      shareability = scoped.shareability;
      return std::move(scoped.inner);
    }
  }

  isa::Minst inst = *isa::Minst::CreateFetch(state.insn_count + 1U, state.pc, packed, len);
  if (isa::DecodeMinstPacked(packed, static_cast<int>(len * 8U), inst) !=
      isa::MinstCodecStatus::Ok) {
    return std::nullopt;
  }
  inst.MarkStage(isa::MinstStage::Decode);
  return inst;
}

bool ReferenceExecutor::Step(std::optional<std::uint64_t> stop_pc) {
  auto &ctx = *ctx_;
  if (ctx.Terminated()) {
    return false;
  }
  if (stop_pc.has_value() && ctx.State().pc == *stop_pc) {
    ctx.RequestTerminate(ctx.ExitCode(), "stop_pc");
    return false;
  }

  std::optional<isa::PtoScalarShareability> shareability;
  auto inst = FetchDecode(shareability);
  if (!inst.has_value()) {
    ctx.RequestTerminate(1, "fetch_decode_failed");
    return false;
  }
  Execute(*inst, shareability);
  ctx.AdvanceCycle();
  return !ctx.Terminated();
}

void ReferenceExecutor::Run(std::optional<std::uint64_t> stop_pc,
                            std::optional<std::uint64_t> max_cycles) {
  auto &ctx = *ctx_;
  while (!ctx.Terminated()) {
    if (max_cycles.has_value() && ctx.Cycle() >= *max_cycles) {
      ctx.RequestTerminate(ctx.ExitCode(), "max_cycles");
      break;
    }
    if (!Step(stop_pc)) {
      break;
    }
  }
}

void ReferenceExecutor::Execute(isa::Minst &inst,
                                std::optional<isa::PtoScalarShareability> shareability) {
  auto &ctx = *ctx_;
  auto &state = ctx.State();
  if (shareability.has_value()) {
    const auto *form = LookupScopedSingleAccess(inst.form_id);
    if (form == nullptr) {
      ctx.RequestTerminate(1, std::string("unsupported_scoped_instruction:") +
                                  std::string(inst.mnemonic));
      return;
    }
    const auto current_pc = state.pc;
    const auto immediate = inst.GetFieldSigned(FieldName(form->immediate_field));
    const auto value_register = inst.GetFieldUnsigned(FieldName(form->value_field));
    const auto base_register = form->base_field == ScopedRegisterField::None
                                   ? std::optional<std::uint64_t>{0}
                                   : inst.GetFieldUnsigned(FieldName(form->base_field));
    if (!immediate.has_value() || !value_register.has_value() || !base_register.has_value()) {
      ctx.RequestTerminate(1, "scoped_operand_decode_failed");
      return;
    }
    const auto base = form->is_pcr ? current_pc & ~UINT64_C(3) : ReadGpr(state, *base_register);
    const auto offset = static_cast<std::uint64_t>(*immediate) << form->offset_scale;
    const auto address = base + offset;
    ScopedMemoryRequest request{
        .shareability = *shareability,
        .status = ScopedMemoryAccessStatus::AccessFault,
        .source_pc = current_pc,
        .wire_bits = inst.raw_bits,
        .address = address,
        .value = 0,
        .size = form->size,
        .is_load = form->is_load,
        .is_store = !form->is_load,
    };
    const bool range_wraps =
        form->size == 0 || address > std::numeric_limits<std::uint64_t>::max() - (form->size - 1U);
    if (range_wraps) {
      ctx.RecordScopedMemoryRequest(request);
      ctx.RequestTerminate(1, "scoped_memory_access_fault");
      return;
    }
    if (address % form->size != 0) {
      request.status = ScopedMemoryAccessStatus::AlignmentFault;
      ctx.RecordScopedMemoryRequest(request);
      ctx.RequestTerminate(1, "scoped_memory_alignment_fault");
      return;
    }

    auto commit_record = MakeMinstRecord(inst, ctx.Cycle(), ResolveBlockKind(inst, state),
                                         ResolveLaneId(ResolveBlockKind(inst, state)));
    if (form->is_load) {
      const auto raw_value = ReadMemoryValue(ctx, address, form->size);
      if (!raw_value.has_value()) {
        ctx.RecordScopedMemoryRequest(request);
        ctx.RequestTerminate(1, "scoped_memory_access_fault");
        return;
      }
      const auto value = NormalizeLoad(*raw_value, form->size, form->signed_load);
      WriteScalarDestination(state, *value_register, value);
      request.value = *raw_value;
      commit_record.dst0.data = value;
      commit_record.memory.valid = 1;
      commit_record.memory.is_load = 1;
      commit_record.memory.addr = address;
      commit_record.memory.size = form->size;
      commit_record.memory.rdata = value;
    } else {
      const auto value = ReadGpr(state, *value_register);
      const auto mask = form->size == 8 ? std::numeric_limits<std::uint64_t>::max()
                                        : (UINT64_C(1) << (form->size * 8U)) - 1U;
      request.value = value & mask;
      WriteMemoryValue(ctx, address, form->size, request.value);
      commit_record.memory.valid = 1;
      commit_record.memory.is_store = 1;
      commit_record.memory.addr = address;
      commit_record.memory.size = form->size;
      commit_record.memory.wdata = request.value;
    }
    request.status = ScopedMemoryAccessStatus::Committed;
    ctx.RecordScopedMemoryRequest(request);
    state.insn_pc_next = current_pc + isa::PtoScopedScalar::CodeBytes;
    ++state.insn_count;
    state.pc = state.insn_pc_next;
    inst.next_pc = state.pc;
    inst.MarkRetired();
    std::strncpy(commit_record.lifecycle, "retired", sizeof(commit_record.lifecycle) - 1U);
    ctx.AppendRecord(commit_record);
    return;
  }
  if (inst.mnemonic == "SDI" && !options_.enable_pto_scoped_scalar) {
    inst.annotation = "instruction decoded; reference executor semantics are not implemented";
    ctx.RequestTerminate(1, "unsupported_instruction:SDI");
    return;
  }
  if (!IsImplemented(inst)) {
    inst.annotation = "instruction decoded; reference executor semantics are not implemented";
    ctx.RequestTerminate(1, std::string("unsupported_instruction:") + std::string(inst.mnemonic));
    return;
  }
  const auto current_pc = state.pc;
  state.insn_pc_next = inst.next_pc;
  ++state.insn_count;

  auto commit_record = MakeMinstRecord(inst, ctx.Cycle(), ResolveBlockKind(inst, state),
                                       ResolveLaneId(ResolveBlockKind(inst, state)));

  const auto current_block_kind = ResolveBlockKind(inst, state);
  state.block_kind = current_block_kind;
  state.lane_id = ResolveLaneId(current_block_kind);

  if (inst.mnemonic == "C.BSTART.STD") {
    state.bpc = current_pc;
    state.brtype = static_cast<std::uint32_t>(GetUnsignedAny(inst, {"BrType"}).value_or(0));
    state.blocktype = 0;
    state.block_kind = "scalar";
    state.lane_id = -1;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "C.BSTART.VPAR") {
    state.bpc = current_pc;
    state.block_kind = "vpar";
    state.lane_id = 0;
    commit_record.block_kind[0] = 0;
    std::strncpy(commit_record.block_kind, "vpar", sizeof(commit_record.block_kind) - 1U);
    commit_record.lane_id = 0;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "C.BSTART.VSEQ") {
    state.bpc = current_pc;
    state.block_kind = "vseq";
    state.lane_id = 0;
    commit_record.block_kind[0] = 0;
    std::strncpy(commit_record.block_kind, "vseq", sizeof(commit_record.block_kind) - 1U);
    commit_record.lane_id = 0;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "C.BSTART") {
    const auto offset = GetSignedAny(inst, {"simm12"}).value_or(0);
    state.tgt = static_cast<std::uint64_t>(static_cast<std::int64_t>(current_pc) + (offset * 2));
    state.bpc = current_pc;
    state.pc = inst.next_pc;
    if (inst.asm_template.find("DIRECT") != std::string_view::npos) {
      state.blocktype = 1;
    } else {
      state.blocktype = 2;
    }
  } else if (inst.mnemonic == "C.BSTOP") {
    if (state.blocktype == 1) {
      state.pc = state.tgt;
    } else if (state.blocktype == 2 && state.carg != 0) {
      state.pc = state.tgt;
    } else {
      state.pc = inst.next_pc;
    }
    state.blocktype = 0;
    state.carg = 0;
  } else if (inst.mnemonic == "C.MOVI") {
    const auto value = static_cast<std::uint64_t>(GetSignedAny(inst, {"simm5"}).value_or(0));
    WriteScalarDestination(state, inst.dsts.empty() ? 0 : inst.dsts.front().value, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "C.MOVR") {
    const auto value = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    WriteScalarDestination(state, inst.dsts.empty() ? 0 : inst.dsts.front().value, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "LUI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto imm = GetSignedAny(inst, {"simm20", "imm20", "simm", "imm"}).value_or(0);
    const auto value = static_cast<std::uint64_t>(imm) << 12U;
    WriteScalarDestination(state, rd, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "HL.LUI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto imm = GetUnsignedAny(inst, {"imm32", "imm"}).value_or(0);
    const auto value = (imm & UINT64_C(0xffffffff)) << 32U;
    WriteScalarDestination(state, rd, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "HL.LIU") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto imm = GetUnsignedAny(inst, {"uimm32", "imm"}).value_or(0);
    const auto value = imm & UINT64_C(0xffffffff);
    WriteScalarDestination(state, rd, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "HL.LIS") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto imm = GetSignedAny(inst, {"simm32", "imm"}).value_or(0);
    const auto value = static_cast<std::uint64_t>(imm);
    WriteScalarDestination(state, rd, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "ADDI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto rs = inst.srcs.empty() ? 0 : inst.srcs.front().value;
    const auto imm = GetSignedAny(inst, {"uimm", "uimm12", "simm", "imms", "simm12"}).value_or(0);
    const auto value = ReadGpr(state, rs) + static_cast<std::uint64_t>(imm);
    WriteScalarDestination(state, rd, value);
    commit_record.dst0.data = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "LWI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto base = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto off =
        static_cast<std::uint64_t>(GetSignedAny(inst, {"simm", "simm12"}).value_or(0)) * 4U;
    const auto addr = base + off;
    const auto raw_value = ctx.Read32(addr).value_or(0);
    const auto value =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(raw_value)));
    WriteScalarDestination(state, rd, value);
    commit_record.dst0.data = value;
    commit_record.memory.valid = 1;
    commit_record.memory.is_load = 1;
    commit_record.memory.addr = addr;
    commit_record.memory.size = 4;
    commit_record.memory.rdata = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "SWI") {
    const auto value = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto base = ReadGpr(state, inst.srcs.size() > 1U ? inst.srcs[1].value : 0);
    const auto off =
        static_cast<std::uint64_t>(GetSignedAny(inst, {"simm", "simm12"}).value_or(0)) * 4U;
    const auto addr = base + off;
    ctx.Write32(addr, static_cast<std::uint32_t>(value));
    commit_record.memory.valid = 1;
    commit_record.memory.is_store = 1;
    commit_record.memory.addr = addr;
    commit_record.memory.size = 4;
    commit_record.memory.wdata = static_cast<std::uint32_t>(value);
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "SDI") {
    const auto value = ReadGpr(state, GetUnsignedAny(inst, {"SrcL"}).value_or(0));
    const auto base = ReadGpr(state, GetUnsignedAny(inst, {"SrcR"}).value_or(0));
    const auto off = static_cast<std::uint64_t>(GetSignedAny(inst, {"simm12"}).value_or(0)) * 8U;
    const auto addr = base + off;
    ctx.Write64(addr, value);
    commit_record.memory.valid = 1;
    commit_record.memory.is_store = 1;
    commit_record.memory.addr = addr;
    commit_record.memory.size = 8;
    commit_record.memory.wdata = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "SETC.NE") {
    const auto lhs = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto rhs = ReadGpr(state, inst.srcs.size() > 1U ? inst.srcs[1].value : 0);
    state.carg = lhs != rhs ? 1U : 0U;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "SETC.LTU") {
    const auto lhs = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto rhs = ReadGpr(state, inst.srcs.size() > 1U ? inst.srcs[1].value : 0);
    state.carg = lhs < rhs ? 1U : 0U;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "SSRSET") {
    const auto value = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto ssr_id = GetUnsignedAny(inst, {"SSR_ID"}).value_or(0);
    if (ssr_id < kLinxSsrCount) {
      state.ssr_acr[state.acr][ssr_id] = value;
    }
    std::strncpy(commit_record.block_kind, "sys", sizeof(commit_record.block_kind) - 1U);
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "MCOPY") {
    const auto dst_addr = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs[0].value);
    const auto src_addr = ReadGpr(state, inst.srcs.size() > 1U ? inst.srcs[1].value : 0);
    const auto count = ReadGpr(state, inst.srcs.size() > 2U ? inst.srcs[2].value : 0);
    for (std::uint64_t idx = 0; idx < count; ++idx) {
      ctx.Write8(dst_addr + idx, ctx.Read8(src_addr + idx).value_or(0));
    }
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "MSET") {
    const auto dst_addr = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs[0].value);
    const auto value =
        static_cast<std::uint8_t>(ReadGpr(state, inst.srcs.size() > 1U ? inst.srcs[1].value : 0));
    const auto count = ReadGpr(state, inst.srcs.size() > 2U ? inst.srcs[2].value : 0);
    for (std::uint64_t idx = 0; idx < count; ++idx) {
      ctx.Write8(dst_addr + idx, value);
    }
    state.pc = inst.next_pc;
  } else if (IsTileHeader(inst) || inst.mnemonic == "B.TEXT") {
    const auto header_block_kind =
        inst.mnemonic == "B.TEXT" ? std::string("tlsu") : current_block_kind;
    state.block_kind = header_block_kind;
    state.pc = inst.next_pc;
    commit_record.block_kind[0] = 0;
    std::strncpy(commit_record.block_kind, header_block_kind.c_str(),
                 sizeof(commit_record.block_kind) - 1U);
  }

  inst.MarkRetired();
  std::strncpy(commit_record.lifecycle, "retired", sizeof(commit_record.lifecycle) - 1U);
  if (ctx.Terminated()) {
    commit_record.trap.valid = 0;
  }
  ctx.AppendRecord(commit_record);
}

void ExecutorBackedSim::OnProgramLoaded(const ProgramImage &image) {
  ctx_->LoadProgram(image);
}

void ExecutorBackedSim::ResetSystem() {
  ctx_->Reset();
}

void ExecutorBackedSim::RunReference(std::optional<std::uint64_t> stop_pc) {
  (void)executor_.Step(stop_pc);
}

bool ExecutorBackedSim::NeedTerminate() const {
  return ctx_->Terminated();
}

void ExecutorBackedSim::ReportSystem() {
  if (!ctx_->LastError().empty()) {
    Logger().Emit(LogLevel::Info,
                  LogContext{.cycle = Cycle(), .module = "executor", .stage = "report"},
                  "termination=" + ctx_->LastError() + " exit=" + std::to_string(ctx_->ExitCode()));
  }
}

} // namespace linx::model::emulator
