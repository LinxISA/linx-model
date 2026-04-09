#include "linx/model/emulator/reference_executor.hpp"

#include <algorithm>
#include <cstring>
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

void WriteGpr(LinxState &state, std::uint64_t idx, std::uint64_t value) {
  if (idx == 0 || idx >= kLinxGprCount) {
    if (idx >= 24 && idx < 28) {
      state.tq[idx - 24] = value;
      return;
    }
    if (idx >= 28 && idx < 32) {
      state.uq[idx - 28] = value;
      return;
    }
    return;
  }
  state.gpr[idx] = value;
}

std::string ResolveBlockKind(const isa::Minst &inst, const LinxState &state) {
  if (inst.mnemonic == "C.BSTART.VPAR") {
    return "vpar";
  }
  if (inst.mnemonic == "C.BSTART.VSEQ") {
    return "vseq";
  }
  if (inst.mnemonic == "BSTART.TLOAD" || inst.mnemonic == "BSTART.TMA" ||
      inst.mnemonic == "BSTART.TEPL" || inst.mnemonic == "BSTART.CUBE") {
    return "tma";
  }
  if (inst.mnemonic == "SSRSET" || inst.mnemonic == "HL.SSRSET") {
    return "sys";
  }
  return state.block_kind;
}

int ResolveLaneId(std::string_view block_kind) {
  return (block_kind == "vpar" || block_kind == "vseq") ? 0 : -1;
}

} // namespace

std::optional<isa::Minst> ReferenceExecutor::FetchDecode() const {
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

  auto inst = FetchDecode();
  if (!inst.has_value()) {
    ctx.RequestTerminate(1, "fetch_decode_failed");
    return false;
  }
  Execute(*inst);
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

void ReferenceExecutor::Execute(isa::Minst &inst) {
  auto &ctx = *ctx_;
  auto &state = ctx.State();
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
    WriteGpr(state, inst.dsts.empty() ? 0 : inst.dsts.front().value,
             static_cast<std::uint64_t>(GetSignedAny(inst, {"simm5"}).value_or(0)));
    commit_record.dst0.data = ReadGpr(state, inst.dsts.empty() ? 0 : inst.dsts.front().value);
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "LUI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto imm = GetSignedAny(inst, {"simm20", "imm20", "simm", "imm"}).value_or(0);
    WriteGpr(state, rd, static_cast<std::uint64_t>(imm) << 12U);
    commit_record.dst0.data = ReadGpr(state, rd);
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "HL.LUI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto imm = GetUnsignedAny(inst, {"imm", "simm22", "simm"}).value_or(0);
    if (rd >= kLinxGprCount) {
      state.tq[0] = imm;
      commit_record.dst0.data = state.tq[0];
    } else {
      WriteGpr(state, rd, imm);
      commit_record.dst0.data = ReadGpr(state, rd);
    }
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "ADDI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto rs = inst.srcs.empty() ? 0 : inst.srcs.front().value;
    const auto imm = GetSignedAny(inst, {"uimm", "uimm12", "simm", "imms", "simm12"}).value_or(0);
    WriteGpr(state, rd, ReadGpr(state, rs) + static_cast<std::uint64_t>(imm));
    commit_record.dst0.data = ReadGpr(state, rd);
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "LWI") {
    const auto rd = inst.dsts.empty() ? 0 : inst.dsts.front().value;
    const auto base = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto off = GetSignedAny(inst, {"simm", "simm12"}).value_or(0);
    const auto addr = static_cast<std::uint64_t>(static_cast<std::int64_t>(base) + off);
    const auto value = ctx.Read32(addr).value_or(0);
    WriteGpr(state, rd, value);
    commit_record.dst0.data = ReadGpr(state, rd);
    commit_record.memory.valid = 1;
    commit_record.memory.is_load = 1;
    commit_record.memory.addr = addr;
    commit_record.memory.size = 4;
    commit_record.memory.rdata = value;
    state.pc = inst.next_pc;
  } else if (inst.mnemonic == "SWI") {
    const auto value = ReadGpr(state, inst.srcs.empty() ? 0 : inst.srcs.front().value);
    const auto base = ReadGpr(state, inst.srcs.size() > 1U ? inst.srcs[1].value : 0);
    const auto off = GetSignedAny(inst, {"simm", "simm12"}).value_or(0);
    const auto addr = static_cast<std::uint64_t>(static_cast<std::int64_t>(base) + off);
    ctx.Write32(addr, static_cast<std::uint32_t>(value));
    commit_record.memory.valid = 1;
    commit_record.memory.is_store = 1;
    commit_record.memory.addr = addr;
    commit_record.memory.size = 4;
    commit_record.memory.wdata = static_cast<std::uint32_t>(value);
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
  } else if (inst.mnemonic == "BSTART.TLOAD" || inst.mnemonic == "B.TEXT") {
    state.block_kind = "tma";
    state.pc = inst.next_pc;
    std::strncpy(commit_record.block_kind, "tma", sizeof(commit_record.block_kind) - 1U);
  } else {
    state.pc = inst.next_pc;
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
