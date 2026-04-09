#pragma once

#include <memory>
#include <optional>

#include "linx/model/emulator/execution_context.hpp"
#include "linx/model/isa/codec.hpp"
#include "linx/model/sim_system.hpp"

namespace linx::model::emulator {

class ReferenceExecutor {
public:
  explicit ReferenceExecutor(ExecutionContext::Ptr ctx) : ctx_(std::move(ctx)) {
    LINX_MODEL_ASSERT(ctx_ != nullptr);
  }

  [[nodiscard]] bool Step(std::optional<std::uint64_t> stop_pc = std::nullopt);
  void Run(std::optional<std::uint64_t> stop_pc = std::nullopt,
           std::optional<std::uint64_t> max_cycles = std::nullopt);

private:
  [[nodiscard]] std::optional<isa::Minst> FetchDecode() const;
  void Execute(isa::Minst &inst);

  ExecutionContext::Ptr ctx_;
};

class ExecutorBackedSim : public SimSystem {
public:
  void OnProgramLoaded(const ProgramImage &image) override;
  void ResetSystem() override;
  void RunReference(std::optional<std::uint64_t> stop_pc) override;
  bool NeedTerminate() const override;
  void ReportSystem() override;
  [[nodiscard]] ExecutionContext &Context() noexcept { return *ctx_; }
  [[nodiscard]] const ExecutionContext &Context() const noexcept { return *ctx_; }

private:
  ExecutionContext::Ptr ctx_{std::make_shared<ExecutionContext>()};
  ReferenceExecutor executor_{ctx_};
};

} // namespace linx::model::emulator
