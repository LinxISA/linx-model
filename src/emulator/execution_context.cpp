#include "linx/model/emulator/execution_context.hpp"

#include <filesystem>

#include "linx/model/sim_assert.hpp"

namespace linx::model::emulator {

namespace {

constexpr std::uint64_t kTestFinisherAddress = 0x10009000ULL;
constexpr std::uint32_t kFinisherStatusMask = 0xffffU;
constexpr std::uint32_t kFinisherCodeShift = 16U;
constexpr std::uint32_t kFinisherFail = 0x3333U;
constexpr std::uint32_t kFinisherPass = 0x5555U;
constexpr std::uint32_t kFinisherReset = 0x7777U;

void HandleTestFinisherWrite(ExecutionContext &ctx, std::uint64_t addr, std::uint32_t value) {
  if (addr != kTestFinisherAddress) {
    return;
  }

  const auto status = value & kFinisherStatusMask;
  const auto code = static_cast<int>(value >> kFinisherCodeShift);
  switch (status) {
  case kFinisherPass:
    ctx.RequestTerminate(0, "finisher_pass");
    break;
  case kFinisherFail:
    ctx.RequestTerminate(code == 0 ? 1 : code, "finisher_fail");
    break;
  case kFinisherReset:
    ctx.RequestTerminate(code == 0 ? 1 : code, "finisher_reset");
    break;
  default:
    break;
  }
}

} // namespace

void ExecutionContext::Reset() {
  state_->Reset();
  memory_.clear();
  committed_.clear();
  last_committed_.reset();
  cycle_ = 0;
  exit_code_ = 0;
  terminated_ = false;
  last_error_.clear();
  if (has_program_) {
    LoadProgram(program_);
  }
}

void ExecutionContext::LoadProgram(const ProgramImage &image) {
  program_ = image;
  has_program_ = true;
  memory_.clear();
  committed_.clear();
  last_committed_.reset();
  state_->Reset();
  state_->pc = image.entry_point;
  for (const auto &section : image.sections) {
    for (std::size_t idx = 0; idx < section.bytes.size(); ++idx) {
      memory_[section.address + idx] = section.bytes[idx];
    }
  }
}

void ExecutionContext::SetTracePath(std::string path) {
  trace_stream_.reset();
  trace_dump_stream_.reset();
  if (path.empty()) {
    return;
  }
  std::filesystem::path fs_path(path);
  if (fs_path.has_parent_path()) {
    std::filesystem::create_directories(fs_path.parent_path());
  }
  trace_stream_.emplace(path, std::ios::out | std::ios::trunc);
  LINX_MODEL_ASSERT_MSG(trace_stream_->good(), "failed to open minst trace jsonl output");
  const auto dump_path =
      fs_path.has_extension() ? fs_path.replace_extension(".dump") : fs_path += ".dump";
  trace_dump_stream_.emplace(dump_path, std::ios::out | std::ios::trunc);
  LINX_MODEL_ASSERT_MSG(trace_dump_stream_->good(), "failed to open minst trace dump output");
}

void ExecutionContext::AppendRecord(const MinstRecord &record) {
  committed_.push_back(record);
  last_committed_ = record;
  if (trace_stream_.has_value() && trace_stream_->good()) {
    WriteMinstRecordJson(*trace_stream_, record);
    *trace_stream_ << '\n';
    trace_stream_->flush();
  }
  if (trace_dump_stream_.has_value() && trace_dump_stream_->good()) {
    WriteMinstRecordDump(*trace_dump_stream_, record);
    *trace_dump_stream_ << '\n';
    trace_dump_stream_->flush();
  }
}

void ExecutionContext::RequestTerminate(int exit_code, std::string reason) {
  terminated_ = true;
  exit_code_ = exit_code;
  last_error_ = std::move(reason);
}

std::optional<std::uint8_t> ExecutionContext::Read8(std::uint64_t addr) const {
  const auto it = memory_.find(addr);
  if (it == memory_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::uint16_t> ExecutionContext::Read16(std::uint64_t addr) const {
  const auto lo = Read8(addr);
  const auto hi = Read8(addr + 1);
  if (!lo.has_value() || !hi.has_value()) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(*lo) | (static_cast<std::uint16_t>(*hi) << 8U);
}

std::optional<std::uint32_t> ExecutionContext::Read32(std::uint64_t addr) const {
  std::uint32_t value = 0;
  for (std::size_t idx = 0; idx < 4; ++idx) {
    const auto byte = Read8(addr + idx);
    if (!byte.has_value()) {
      return std::nullopt;
    }
    value |= static_cast<std::uint32_t>(*byte) << (idx * 8U);
  }
  return value;
}

std::optional<std::uint64_t> ExecutionContext::Read64(std::uint64_t addr) const {
  std::uint64_t value = 0;
  for (std::size_t idx = 0; idx < 8; ++idx) {
    const auto byte = Read8(addr + idx);
    if (!byte.has_value()) {
      return std::nullopt;
    }
    value |= static_cast<std::uint64_t>(*byte) << (idx * 8U);
  }
  return value;
}

void ExecutionContext::Write8(std::uint64_t addr, std::uint8_t value) {
  memory_[addr] = value;
}

void ExecutionContext::Write32(std::uint64_t addr, std::uint32_t value) {
  for (std::size_t idx = 0; idx < 4; ++idx) {
    Write8(addr + idx, static_cast<std::uint8_t>((value >> (idx * 8U)) & 0xffU));
  }
  HandleTestFinisherWrite(*this, addr, value);
}

void ExecutionContext::Write64(std::uint64_t addr, std::uint64_t value) {
  for (std::size_t idx = 0; idx < 8; ++idx) {
    Write8(addr + idx, static_cast<std::uint8_t>((value >> (idx * 8U)) & 0xffU));
  }
}

} // namespace linx::model::emulator
