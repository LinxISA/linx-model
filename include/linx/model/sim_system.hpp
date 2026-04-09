#pragma once

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "linx/model/logging.hpp"
#include "linx/model/sim_assert.hpp"
#include "linx/model/sim_object.hpp"
#include "linx/model/validation.hpp"

namespace linx::model {

/**
 * @brief Top-level owner for modules, cycle count, logging, and validation.
 */
class SimSystem {
public:
  using module_type = SimObject;
  using size_type = std::size_t;

  SimSystem() : logger_(std::clog) {}
  virtual ~SimSystem() = default;

  void Build() {
    LINX_MODEL_ASSERT(!built_);
    BuildSystem();
    for (auto *module : modules_) {
      LINX_MODEL_ASSERT(module != nullptr);
      module->Build();
    }
    built_ = true;
  }

  void Reset() {
    ResetSystem();
    cycle_ = 0;
    terminate_requested_ = false;
    for (auto *module : modules_) {
      LINX_MODEL_ASSERT(module != nullptr);
      module->Reset();
    }
  }

  void Report() {
    ReportSystem();
    for (auto *module : modules_) {
      LINX_MODEL_ASSERT(module != nullptr);
      module->Report();
    }
  }

  void Step() {
    for (auto *module : modules_) {
      LINX_MODEL_ASSERT(module != nullptr);
      module->Work();
    }

    for (auto *module : modules_) {
      LINX_MODEL_ASSERT(module != nullptr);
      module->Xfer();
    }

    ++cycle_;
  }

  void step() {
    Step();
  }

  virtual void RunReference(std::optional<std::uint64_t> stop_pc) {
    (void)stop_pc;
  }

  void PrintPipeView() const {
    PrintPipeView(default_pipeview_stream_);
  }
  virtual void PrintPipeView(std::ostream &os) const {
    (void)os;
  }

  virtual bool NeedTerminate() const {
    return terminate_requested_;
  }
  bool needTerminate() const {
    return NeedTerminate();
  }

  void RequestTerminate() noexcept {
    terminate_requested_ = true;
  }

  [[nodiscard]] std::uint64_t Cycle() const noexcept {
    return cycle_;
  }
  [[nodiscard]] bool IsBuilt() const noexcept {
    return built_;
  }
  [[nodiscard]] SimLogger &Logger() noexcept {
    return logger_;
  }
  [[nodiscard]] const SimLogger &Logger() const noexcept {
    return logger_;
  }

  [[nodiscard]] size_type ModuleCount() const noexcept {
    return modules_.size();
  }
  [[nodiscard]] const std::vector<module_type *> &Modules() const noexcept {
    return modules_;
  }
  [[nodiscard]] ValidationReport Validate() const;

  module_type &AddModule(module_type &module) {
    module.AttachRuntime(&logger_, &cycle_);
    modules_.push_back(&module);
    return module;
  }

  module_type &AddOwnedModule(std::unique_ptr<module_type> module) {
    LINX_MODEL_ASSERT(module != nullptr);
    module->AttachRuntime(&logger_, &cycle_);
    module_type &ref = *module;
    owned_modules_.push_back(std::move(module));
    modules_.push_back(&ref);
    return ref;
  }

  template <class ModuleT, class... Args> ModuleT &EmplaceOwnedModule(Args &&...args) {
    auto module = std::make_unique<ModuleT>(std::forward<Args>(args)...);
    ModuleT &ref = *module;
    AddOwnedModule(std::move(module));
    return ref;
  }

protected:
  virtual void BuildSystem() {}
  virtual void ResetSystem() {}
  virtual void ReportSystem() {}

private:
  std::vector<module_type *> modules_;
  std::vector<std::unique_ptr<module_type>> owned_modules_;
  SimLogger logger_;
  std::uint64_t cycle_ = 0;
  bool built_ = false;
  bool terminate_requested_ = false;
  inline static std::ostream &default_pipeview_stream_ = std::cout;
};

} // namespace linx::model
