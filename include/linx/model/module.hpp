#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "linx/model/port_info.hpp"
#include "linx/model/sim_assert.hpp"
#include "linx/model/sim_object.hpp"
#include "linx/model/sim_queue.hpp"
#include "linx/model/validation.hpp"

namespace linx::model {

/**
 * @brief Recursive, queue-wired module for cycle-accurate simulation.
 *
 * A parent module owns child modules and the concrete `SimQueue` instances that
 * wire them together. Child modules only access queue pointers bound to named
 * `input`, `output`, and `inner` ports.
 */
template <class Derived, class PortT> class Module : public SimObject {
public:
  using Queue = SimQueue<PortT>;
  using size_type = std::size_t;

  explicit Module(std::string module_name = "module") : SimObject(std::move(module_name)) {}
  ~Module() override = default;

  [[nodiscard]] bool IsQueueBased() const noexcept override {
    return true;
  }

  void Build() override {
    LINX_MODEL_ASSERT(!built_);
    BuildSelf();
    for (auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      submodule->Build();
    }
    ResetEventState();
    built_ = true;
  }

  void Reset() override {
    ResetSelf();
    for (auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      submodule->Reset();
    }
    for (auto &queue : owned_queues_) {
      LINX_MODEL_ASSERT(queue != nullptr);
      queue->Reset();
    }
    ResetEventState();
  }

  void Report() override {
    ReportSelf();
    for (auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      submodule->Report();
    }
  }

  void Work() override {
    active_submodules_.clear();
    ran_work_self_last_cycle_ = HasInputEvent();
    if (ran_work_self_last_cycle_) {
      WorkSelf();
      force_activate_self_ = false;
      CaptureObservedInputEpochs();
    }

    for (auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      if (submodule->HasPendingWorkRecursive()) {
        active_submodules_.push_back(submodule.get());
        submodule->Work();
      } else {
        submodule->AdvanceQueuesOnly();
      }
    }
    for (auto &queue : owned_queues_) {
      LINX_MODEL_ASSERT(queue != nullptr);
      queue->Work();
    }
  }

  void Xfer() override {
    if (ran_work_self_last_cycle_) {
      XferSelf();
    }
    for (auto *submodule : active_submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      submodule->Xfer();
    }
    for (auto &queue : owned_queues_) {
      LINX_MODEL_ASSERT(queue != nullptr);
      queue->Xfer();
    }
  }

  void CollectValidationIssues(ValidationReport &report) const override {
    ValidateDeclaredPorts(report, input_ports_, inputs_, ModuleName());
    ValidateDeclaredPorts(report, output_ports_, outputs_, ModuleName());
    ValidateDeclaredPorts(report, inner_ports_, inners_, ModuleName());

    if (require_io_contract_ && (input_ports_.empty() || output_ports_.empty())) {
      report.AddError(std::string(ModuleName()),
                      "module must declare at least one input and one output");
    }

    for (const auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      submodule->CollectValidationIssues(report);
    }
  }

  /**
   * @brief Declare a named input signal for this module.
   */
  size_type DescribeInput(std::string name, std::string description) {
    return DescribePort(input_ports_, inputs_, PortDirection::Input, std::move(name),
                        std::move(description));
  }

  /**
   * @brief Declare a named output signal for this module.
   */
  size_type DescribeOutput(std::string name, std::string description) {
    return DescribePort(output_ports_, outputs_, PortDirection::Output, std::move(name),
                        std::move(description));
  }

  /**
   * @brief Declare a named internal queue signal for this module.
   */
  size_type DescribeInner(std::string name, std::string description) {
    return DescribePort(inner_ports_, inners_, PortDirection::Inner, std::move(name),
                        std::move(description));
  }

  /**
   * @brief Declare and bind a named input signal in one call.
   */
  Queue *AddInput(Queue *queue, std::string name, std::string description) {
    const size_type idx = DescribeInput(std::move(name), std::move(description));
    BindInput(idx, queue);
    return queue;
  }

  /**
   * @brief Declare and bind a named output signal in one call.
   */
  Queue *AddOutput(Queue *queue, std::string name, std::string description) {
    const size_type idx = DescribeOutput(std::move(name), std::move(description));
    BindOutput(idx, queue);
    return queue;
  }

  /**
   * @brief Declare and bind a named internal queue signal in one call.
   */
  Queue *AddInner(Queue *queue, std::string name, std::string description) {
    const size_type idx = DescribeInner(std::move(name), std::move(description));
    BindInner(idx, queue);
    return queue;
  }

  void BindInput(size_type idx, Queue *queue) {
    BindPort(idx, queue, inputs_);
  }
  void BindOutput(size_type idx, Queue *queue) {
    BindPort(idx, queue, outputs_);
  }
  void BindInner(size_type idx, Queue *queue) {
    BindPort(idx, queue, inners_);
  }

  /**
   * @brief Allocate a queue owned by the parent module.
   */
  template <typename... Args> Queue *CreateOwnedQueue(Args &&...args) {
    owned_queues_.push_back(std::make_unique<Queue>(std::forward<Args>(args)...));
    owned_queues_.back()->AttachRuntime(this->RuntimeLogger(), this->RuntimeCyclePtr());
    return owned_queues_.back().get();
  }

  Derived &AddSubmodule(std::unique_ptr<Derived> submodule) {
    LINX_MODEL_ASSERT(submodule != nullptr);
    submodule->AttachRuntime(this->RuntimeLogger(), this->RuntimeCyclePtr());
    submodules_.push_back(std::move(submodule));
    return *submodules_.back();
  }

  void ConnectInput(Derived &child, size_type child_input_idx, Queue *queue) {
    child.BindInput(child_input_idx, queue);
  }
  void ConnectOutput(Derived &child, size_type child_output_idx, Queue *queue) {
    child.BindOutput(child_output_idx, queue);
  }
  void ConnectInner(Derived &child, size_type child_inner_idx, Queue *queue) {
    child.BindInner(child_inner_idx, queue);
  }

  [[nodiscard]] Queue *Input(size_type idx) {
    LINX_MODEL_ASSERT(idx < inputs_.size());
    LINX_MODEL_ASSERT(inputs_[idx] != nullptr);
    return inputs_[idx];
  }

  [[nodiscard]] const Queue *Input(size_type idx) const {
    LINX_MODEL_ASSERT(idx < inputs_.size());
    LINX_MODEL_ASSERT(inputs_[idx] != nullptr);
    return inputs_[idx];
  }

  [[nodiscard]] Queue *Output(size_type idx) {
    LINX_MODEL_ASSERT(idx < outputs_.size());
    LINX_MODEL_ASSERT(outputs_[idx] != nullptr);
    return outputs_[idx];
  }

  [[nodiscard]] const Queue *Output(size_type idx) const {
    LINX_MODEL_ASSERT(idx < outputs_.size());
    LINX_MODEL_ASSERT(outputs_[idx] != nullptr);
    return outputs_[idx];
  }

  [[nodiscard]] Queue *Inner(size_type idx) {
    LINX_MODEL_ASSERT(idx < inners_.size());
    LINX_MODEL_ASSERT(inners_[idx] != nullptr);
    return inners_[idx];
  }

  [[nodiscard]] const Queue *Inner(size_type idx) const {
    LINX_MODEL_ASSERT(idx < inners_.size());
    LINX_MODEL_ASSERT(inners_[idx] != nullptr);
    return inners_[idx];
  }

  [[nodiscard]] const PortInfo &InputPortInfo(size_type idx) const {
    LINX_MODEL_ASSERT(idx < input_ports_.size());
    return input_ports_[idx];
  }

  [[nodiscard]] const PortInfo &OutputPortInfo(size_type idx) const {
    LINX_MODEL_ASSERT(idx < output_ports_.size());
    return output_ports_[idx];
  }

  [[nodiscard]] const PortInfo &InnerPortInfo(size_type idx) const {
    LINX_MODEL_ASSERT(idx < inner_ports_.size());
    return inner_ports_[idx];
  }

  [[nodiscard]] const std::vector<PortInfo> &InputPorts() const noexcept {
    return input_ports_;
  }
  [[nodiscard]] const std::vector<PortInfo> &OutputPorts() const noexcept {
    return output_ports_;
  }
  [[nodiscard]] const std::vector<PortInfo> &InnerPorts() const noexcept {
    return inner_ports_;
  }

  [[nodiscard]] Derived &Submodule(size_type idx) {
    LINX_MODEL_ASSERT(idx < submodules_.size());
    LINX_MODEL_ASSERT(submodules_[idx] != nullptr);
    return *submodules_[idx];
  }

  [[nodiscard]] const Derived &Submodule(size_type idx) const {
    LINX_MODEL_ASSERT(idx < submodules_.size());
    LINX_MODEL_ASSERT(submodules_[idx] != nullptr);
    return *submodules_[idx];
  }

  [[nodiscard]] Queue *OwnedQueue(size_type idx) {
    LINX_MODEL_ASSERT(idx < owned_queues_.size());
    LINX_MODEL_ASSERT(owned_queues_[idx] != nullptr);
    return owned_queues_[idx].get();
  }

  [[nodiscard]] const Queue *OwnedQueue(size_type idx) const {
    LINX_MODEL_ASSERT(idx < owned_queues_.size());
    LINX_MODEL_ASSERT(owned_queues_[idx] != nullptr);
    return owned_queues_[idx].get();
  }

  [[nodiscard]] const std::vector<Queue *> &Inputs() const noexcept {
    return inputs_;
  }
  [[nodiscard]] const std::vector<Queue *> &Outputs() const noexcept {
    return outputs_;
  }
  [[nodiscard]] const std::vector<Queue *> &Inners() const noexcept {
    return inners_;
  }
  [[nodiscard]] const std::vector<std::unique_ptr<Derived>> &Submodules() const noexcept {
    return submodules_;
  }
  [[nodiscard]] const std::vector<std::unique_ptr<Queue>> &OwnedQueues() const noexcept {
    return owned_queues_;
  }

  [[nodiscard]] size_type InputCount() const noexcept {
    return inputs_.size();
  }
  [[nodiscard]] size_type OutputCount() const noexcept {
    return outputs_.size();
  }
  [[nodiscard]] size_type InnerCount() const noexcept {
    return inners_.size();
  }
  [[nodiscard]] size_type SubmoduleCount() const noexcept {
    return submodules_.size();
  }
  [[nodiscard]] size_type OwnedQueueCount() const noexcept {
    return owned_queues_.size();
  }
  [[nodiscard]] size_type ActiveSubmoduleCount() const noexcept {
    return active_submodules_.size();
  }
  [[nodiscard]] bool IsBuilt() const noexcept {
    return built_;
  }
  [[nodiscard]] bool RanWorkSelfLastCycle() const noexcept {
    return ran_work_self_last_cycle_;
  }
  [[nodiscard]] const std::vector<Derived *> &ActiveSubmodules() const noexcept {
    return active_submodules_;
  }

  void SetRequireIOContract(bool enabled) noexcept {
    require_io_contract_ = enabled;
  }
  [[nodiscard]] bool RequireIOContract() const noexcept {
    return require_io_contract_;
  }
  void RequestActivation() noexcept {
    force_activate_self_ = true;
  }

protected:
  virtual void BuildSelf() {}
  virtual void ResetSelf() {}
  virtual void ReportSelf() {}
  virtual void WorkSelf() {}
  virtual void XferSelf() {}

  std::vector<Queue *> inputs_;
  std::vector<Queue *> outputs_;
  std::vector<Queue *> inners_;
  std::vector<std::unique_ptr<Derived>> submodules_;
  std::vector<std::unique_ptr<Queue>> owned_queues_;

private:
  void AdvanceQueuesOnly() {
    active_submodules_.clear();
    ran_work_self_last_cycle_ = false;
    for (auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      if (submodule->HasPendingWorkRecursive()) {
        active_submodules_.push_back(submodule.get());
        submodule->Work();
      } else {
        submodule->AdvanceQueuesOnly();
      }
    }
    for (auto &queue : owned_queues_) {
      LINX_MODEL_ASSERT(queue != nullptr);
      queue->Work();
    }
  }

  [[nodiscard]] bool HasPendingWorkRecursive() const {
    if (HasInputEvent()) {
      return true;
    }
    for (const auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      if (submodule->HasPendingWorkRecursive()) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool HasInputEvent() const {
    return force_activate_self_ || QueuesChanged(inputs_, observed_input_epochs_) ||
           QueuesChanged(inners_, observed_inner_epochs_);
  }

  void CaptureObservedInputEpochs() {
    CaptureQueueEpochs(inputs_, observed_input_epochs_);
    CaptureQueueEpochs(inners_, observed_inner_epochs_);
  }

  void ResetEventState() {
    force_activate_self_ = false;
    ran_work_self_last_cycle_ = false;
    active_submodules_.clear();
    CaptureObservedInputEpochs();
    for (auto &submodule : submodules_) {
      LINX_MODEL_ASSERT(submodule != nullptr);
      submodule->ResetEventState();
    }
  }

  static void CaptureQueueEpochs(const std::vector<Queue *> &queues,
                                 std::vector<std::uint64_t> &epochs) {
    epochs.resize(queues.size(), 0);
    for (size_type idx = 0; idx < queues.size(); ++idx) {
      epochs[idx] = queues[idx] != nullptr ? queues[idx]->VisibleEpoch() : 0;
    }
  }

  [[nodiscard]] static bool QueuesChanged(const std::vector<Queue *> &queues,
                                          const std::vector<std::uint64_t> &epochs) {
    if (queues.size() != epochs.size()) {
      return true;
    }
    for (size_type idx = 0; idx < queues.size(); ++idx) {
      const std::uint64_t current = queues[idx] != nullptr ? queues[idx]->VisibleEpoch() : 0;
      if (current != epochs[idx]) {
        return true;
      }
    }
    return false;
  }

  static bool ContainsQueue(const std::vector<Queue *> &queues, const Queue *queue) {
    return std::find(queues.begin(), queues.end(), queue) != queues.end();
  }

  static void ValidateDeclaredPorts(ValidationReport &report, const std::vector<PortInfo> &ports,
                                    const std::vector<Queue *> &queues,
                                    std::string_view module_name) {
    const std::string component(module_name);
    for (std::size_t i = 0; i < ports.size(); ++i) {
      const auto &port = ports[i];
      if (port.name.empty()) {
        report.AddError(component, std::string(ToString(port.direction)) + " port[" +
                                       std::to_string(i) + "] is missing a signal name");
      }
      if (port.description.empty()) {
        report.AddError(component, std::string(ToString(port.direction)) + " port[" +
                                       std::to_string(i) + "] is missing a description");
      }
      if (i >= queues.size() || queues[i] == nullptr) {
        report.AddError(component, std::string(ToString(port.direction)) + " port '" + port.name +
                                       "' is not bound to a queue");
      }
    }
  }

  size_type DescribePort(std::vector<PortInfo> &ports, std::vector<Queue *> &queues,
                         PortDirection direction, std::string name, std::string description) {
    const size_type idx = ports.size();
    ports.push_back(PortInfo{
        .index = idx,
        .direction = direction,
        .name = std::move(name),
        .description = std::move(description),
    });
    queues.resize(idx + 1, nullptr);
    return idx;
  }

  void BindPort(size_type idx, Queue *queue, std::vector<Queue *> &slots) {
    LINX_MODEL_ASSERT(queue != nullptr);
    LINX_MODEL_ASSERT(idx < slots.size());
    AssertRoleSlotBindable(queue, slots[idx]);
    slots[idx] = queue;
  }

  void AssertRoleSlotBindable(Queue *queue, Queue *current_slot) const {
    LINX_MODEL_ASSERT(queue != nullptr);
    if (current_slot == queue) {
      return;
    }
    LINX_MODEL_ASSERT(!ContainsQueue(inputs_, queue));
    LINX_MODEL_ASSERT(!ContainsQueue(outputs_, queue));
    LINX_MODEL_ASSERT(!ContainsQueue(inners_, queue));
  }

  std::vector<PortInfo> input_ports_;
  std::vector<PortInfo> output_ports_;
  std::vector<PortInfo> inner_ports_;
  std::vector<std::uint64_t> observed_input_epochs_;
  std::vector<std::uint64_t> observed_inner_epochs_;
  std::vector<Derived *> active_submodules_;
  bool built_ = false;
  bool require_io_contract_ = true;
  bool force_activate_self_ = false;
  bool ran_work_self_last_cycle_ = false;
};

} // namespace linx::model
