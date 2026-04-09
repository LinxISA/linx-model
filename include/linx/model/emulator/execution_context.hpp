#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "linx/model/emulator/minst_record.hpp"
#include "linx/model/emulator/state.hpp"
#include "linx/model/program_image.hpp"

namespace linx::model::emulator {

class ExecutionContext {
public:
  using Ptr = std::shared_ptr<ExecutionContext>;
  using StatePtr = std::shared_ptr<LinxState>;

  ExecutionContext() = default;
  ExecutionContext(const ExecutionContext &) = delete;
  ExecutionContext &operator=(const ExecutionContext &) = delete;
  ExecutionContext(ExecutionContext &&) = delete;
  ExecutionContext &operator=(ExecutionContext &&) = delete;

  void Reset();
  void LoadProgram(const ProgramImage &image);

  [[nodiscard]] bool HasProgram() const noexcept { return has_program_; }
  [[nodiscard]] const ProgramImage &Program() const noexcept { return program_; }
  [[nodiscard]] LinxState &State() noexcept { return *state_; }
  [[nodiscard]] const LinxState &State() const noexcept { return *state_; }
  [[nodiscard]] const StatePtr &SharedState() const noexcept { return state_; }
  [[nodiscard]] std::uint64_t Cycle() const noexcept { return cycle_; }
  [[nodiscard]] bool Terminated() const noexcept { return terminated_; }
  [[nodiscard]] int ExitCode() const noexcept { return exit_code_; }
  [[nodiscard]] const std::string &LastError() const noexcept { return last_error_; }
  [[nodiscard]] const std::vector<MinstRecord> &Committed() const noexcept { return committed_; }
  [[nodiscard]] const std::optional<MinstRecord> &LastCommitted() const noexcept { return last_committed_; }

  void SetTracePath(std::string path);
  void AppendRecord(const MinstRecord &record);
  void RequestTerminate(int exit_code, std::string reason = {});
  void AdvanceCycle() noexcept { ++cycle_; }

  [[nodiscard]] std::optional<std::uint8_t> Read8(std::uint64_t addr) const;
  [[nodiscard]] std::optional<std::uint16_t> Read16(std::uint64_t addr) const;
  [[nodiscard]] std::optional<std::uint32_t> Read32(std::uint64_t addr) const;
  [[nodiscard]] std::optional<std::uint64_t> Read64(std::uint64_t addr) const;
  void Write8(std::uint64_t addr, std::uint8_t value);
  void Write32(std::uint64_t addr, std::uint32_t value);
  void Write64(std::uint64_t addr, std::uint64_t value);

private:
  ProgramImage program_{};
  StatePtr state_{std::make_shared<LinxState>()};
  std::unordered_map<std::uint64_t, std::uint8_t> memory_{};
  std::vector<MinstRecord> committed_{};
  std::optional<MinstRecord> last_committed_;
  std::optional<std::ofstream> trace_stream_;
  std::optional<std::ofstream> trace_dump_stream_;
  std::uint64_t cycle_ = 0;
  int exit_code_ = 0;
  bool has_program_ = false;
  bool terminated_ = false;
  std::string last_error_;
};

} // namespace linx::model::emulator
