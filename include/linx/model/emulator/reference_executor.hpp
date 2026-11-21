#pragma once

#include <memory>
#include <optional>

#include "linx/model/emulator/execution_context.hpp"
#include "linx/model/isa/codec.hpp"
#include "linx/model/sim_system.hpp"

namespace linx::model::emulator {

struct ReferenceExecutorOptions {
  bool enable_pto_scoped_scalar = false;
};

class ReferenceExecutor {
public:
  explicit ReferenceExecutor(ExecutionContext::Ptr ctx, ReferenceExecutorOptions options = {})
      : ctx_(std::move(ctx)), options_(options) {
    LINX_MODEL_ASSERT(ctx_ != nullptr);
  }

  [[nodiscard]] bool Step(std::optional<std::uint64_t> stop_pc = std::nullopt);
  void Run(std::optional<std::uint64_t> stop_pc = std::nullopt,
           std::optional<std::uint64_t> max_cycles = std::nullopt);

private:
  [[nodiscard]] std::optional<isa::Minst>
  FetchDecode(std::optional<isa::PtoScalarShareability> &shareability) const;
  void Execute(isa::Minst &inst,
               std::optional<isa::PtoScalarShareability> shareability = std::nullopt);

  ExecutionContext::Ptr ctx_;
  ReferenceExecutorOptions options_{};
};

class ExecutorBackedSim : public SimSystem {
public:
  explicit ExecutorBackedSim(ReferenceExecutorOptions options = {}) : executor_(ctx_, options) {}

  void OnProgramLoaded(const ProgramImage &image) override;
  void ResetSystem() override;
  void RunReference(std::optional<std::uint64_t> stop_pc) override;
  bool NeedTerminate() const override;
  void ReportSystem() override;
  [[nodiscard]] ExecutionContext &Context() noexcept {
    return *ctx_;
  }
  [[nodiscard]] const ExecutionContext &Context() const noexcept {
    return *ctx_;
  }

private:
  ExecutionContext::Ptr ctx_{std::make_shared<ExecutionContext>()};
  ReferenceExecutor executor_;
};

} // namespace linx::model::emulator
