#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "linx/model.hpp"

namespace {

class PipelineModule : public linx::model::Module<PipelineModule, int> {
public:
  explicit PipelineModule(bool root = false)
      : linx::model::Module<PipelineModule, int>(root ? "pipe_root" : "pipe_stage"),
        build_root_(root) {
    DescribeInput("flag_in", "Visible input token for this pipeline stage");
    DescribeOutput("flag_out", "Visible output token for this pipeline stage");
    if (!root) {
      DescribeInner("stage_pipe", "Internal registered token");
    } else {
      DescribeInner("root_pipe", "Top-level internal pipeline link");
    }
  }

  int work_count = 0;
  int xfer_count = 0;

protected:
  void BuildSelf() override {
    if (!build_root_) {
      return;
    }

    auto *input_q = CreateOwnedQueue(8, 0, "pipe_input_q");
    auto *inner_q = CreateOwnedQueue(8, 1, "pipe_inner_q");
    auto *output_q = CreateOwnedQueue(8, 0, "pipe_output_q");

    BindInput(0, input_q);
    BindInner(0, inner_q);
    BindOutput(0, output_q);

    auto &child = AddSubmodule(std::make_unique<PipelineModule>(false));
    ConnectInput(child, 0, input_q);
    ConnectInner(child, 0, inner_q);
    ConnectOutput(child, 0, output_q);
  }

  void WorkSelf() override {
    ++work_count;

    if (build_root_) {
      return;
    }

    INPUT(in0, 0);
    INNER(pipe0, 0);
    OUTPUT(out0, 0);

    if (!pipe0->Empty() && !out0->Full()) {
      const int ready = pipe0->Read();
      out0->Write(ready + 10);
      return;
    }

    if (!in0->Empty() && !pipe0->Full()) {
      const int head = in0->Front();
      pipe0->Write(head + 1);
      in0->Pop();
    }
  }

  void XferSelf() override {
    ++xfer_count;
  }

private:
  bool build_root_ = false;
};

class CountingModule : public linx::model::SimObject {
public:
  explicit CountingModule(std::vector<std::string> *events)
      : linx::model::SimObject("counter"), events_(events) {}

  void Build() override {
    events_->push_back("build");
  }
  void Reset() override {
    events_->push_back("reset");
  }
  void Report() override {
    events_->push_back("report");
  }
  void Work() override {
    events_->push_back("work");
  }
  void Xfer() override {
    events_->push_back("xfer");
  }

private:
  std::vector<std::string> *events_ = nullptr;
};

class ToySim : public linx::model::SimSystem {
public:
  explicit ToySim(int terminate_cycle) : terminate_cycle_(terminate_cycle) {
    pipeline_ = &EmplaceOwnedModule<PipelineModule>(true);
    AddModule(external_counter_);
  }

  void RunReference(std::optional<std::uint64_t> stop_pc) override {
    reference_calls.push_back(stop_pc);
  }

  void PrintPipeView(std::ostream &os) const override {
    os << "cycle=" << Cycle() << '\n';
  }

  bool NeedTerminate() const override {
    return Cycle() >= static_cast<std::uint64_t>(terminate_cycle_);
  }

  PipelineModule &Pipeline() {
    return *pipeline_;
  }

  std::vector<std::optional<std::uint64_t>> reference_calls;

private:
  void BuildSystem() override {
    build_system_calls++;
  }
  void ResetSystem() override {
    reset_system_calls++;
  }
  void ReportSystem() override {
    report_system_calls++;
  }

public:
  int build_system_calls = 0;
  int reset_system_calls = 0;
  int report_system_calls = 0;

private:
  int terminate_cycle_ = 0;
  PipelineModule *pipeline_ = nullptr;
  std::vector<std::string> external_events_;
  CountingModule external_counter_{&external_events_};
};

int RunArgParseSmoke() {
  ToySim sim(2);
  std::ostringstream out;
  std::ostringstream err;

  const char *argv[] = {
      "toy-sim", "--stop-pc", "0x20", "--max-cycles", "2", "--log-level", "debug", "--no-report",
  };

  const int rc = linx::model::RunSimMain(static_cast<int>(std::size(argv)),
                                         const_cast<char **>(argv), sim, out, err);
  if (rc != 0) {
    return 1;
  }
  if (!err.str().empty()) {
    return 2;
  }
  if (sim.Cycle() != 2) {
    return 3;
  }
  if (sim.reference_calls.size() != 2) {
    return 4;
  }
  if (sim.reference_calls[0] != 0x20 || sim.reference_calls[1] != 0x20) {
    return 5;
  }
  if (out.str() != "cycle=1\ncycle=2\n") {
    return 6;
  }
  if (sim.report_system_calls != 0) {
    return 7;
  }
  if (sim.build_system_calls != 1 || sim.reset_system_calls != 1) {
    return 8;
  }
  if (sim.Logger().MinLevel() != linx::model::LogLevel::Debug) {
    return 9;
  }

  return 0;
}

int RunStepOrderSmoke() {
  std::vector<std::string> events;
  CountingModule counter(&events);
  linx::model::SimSystem sim;
  sim.AddModule(counter);
  sim.Build();
  sim.Reset();
  sim.Step();
  sim.Report();

  const std::vector<std::string> expected = {"build", "reset", "work", "xfer", "report"};
  if (events != expected) {
    return 10;
  }

  return 0;
}

int RunInnerQueueSmoke() {
  PipelineModule root(true);
  root.Build();

  if (root.InputCount() != 1 || root.InnerCount() != 1 || root.OutputCount() != 1) {
    return 20;
  }
  if (root.InputPortInfo(0).name != "flag_in" || root.InnerPortInfo(0).name != "root_pipe") {
    return 21;
  }

  root.Input(0)->Write(5);
  root.Work();
  if (!root.Inner(0)->Empty()) {
    return 22;
  }
  if (!root.Output(0)->Empty()) {
    return 23;
  }
  if (root.Submodule(0).RanWorkSelfLastCycle()) {
    return 231;
  }

  root.Work();
  if (root.Inner(0)->Empty()) {
    return 24;
  }
  if (!root.Output(0)->Empty()) {
    return 25;
  }
  if (!root.Submodule(0).RanWorkSelfLastCycle()) {
    return 251;
  }

  root.Work();
  if (root.Output(0)->Empty()) {
    return 26;
  }
  if (root.Output(0)->Read() != 16) {
    return 27;
  }
  if (root.Submodule(0).work_count != 2 || root.Submodule(0).xfer_count != 0) {
    return 28;
  }

  root.Xfer();
  if (root.Submodule(0).xfer_count != 1) {
    return 29;
  }

  return 0;
}

int RunIdleSkipSmoke() {
  PipelineModule root(true);
  root.Build();

  root.Work();
  root.Xfer();
  const int child_work_after_first = root.Submodule(0).work_count;

  root.Work();
  root.Xfer();
  if (root.Submodule(0).work_count != child_work_after_first) {
    return 30;
  }
  if (root.Submodule(0).RanWorkSelfLastCycle()) {
    return 31;
  }

  root.Input(0)->Write(9);
  root.Work();
  if (root.Submodule(0).RanWorkSelfLastCycle()) {
    return 32;
  }

  root.Work();
  if (!root.Submodule(0).RanWorkSelfLastCycle()) {
    return 33;
  }

  return 0;
}

} // namespace

int main() {
  if (RunStepOrderSmoke() != 0) {
    return 1;
  }
  if (RunArgParseSmoke() != 0) {
    return 2;
  }
  if (RunInnerQueueSmoke() != 0) {
    return 3;
  }
  if (RunIdleSkipSmoke() != 0) {
    return 4;
  }
  return 0;
}
