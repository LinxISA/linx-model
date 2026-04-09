#include <memory>

#include "linx/model.hpp"

namespace {

struct Packet {
  explicit Packet(int v) : value(v) {}

  int value = 0;
  int hops = 0;

  void DumpFields(linx::model::PacketDumpWriter &writer) const {
    writer.Field("value", value);
    writer.Field("hops", hops);
  }
};

using PacketPtr = std::unique_ptr<Packet>;

class ChainModule : public linx::model::Module<ChainModule, PacketPtr> {
public:
  explicit ChainModule(bool build_children = false)
      : linx::model::Module<ChainModule, PacketPtr>(build_children ? "chain_root" : "chain_stage"),
        build_children_(build_children) {
    DescribeInput("req_in", "Packet entering this chain stage");
    DescribeOutput("req_out", "Packet leaving this chain stage");
  }

  int reset_count = 0;
  int report_count = 0;
  int work_count = 0;
  int xfer_count = 0;

protected:
  void BuildSelf() override {
    if (!build_children_) {
      return;
    }

    auto *input_queue = CreateOwnedQueue(8, 0, "root_input_q");
    auto *link_queue = CreateOwnedQueue(8, 1, "root_link_q");
    auto *output_queue = CreateOwnedQueue(8, 1, "root_output_q");

    BindInput(0, input_queue);
    BindOutput(0, output_queue);

    auto &stage_a = AddSubmodule(std::make_unique<ChainModule>(false));
    auto &stage_b = AddSubmodule(std::make_unique<ChainModule>(false));

    ConnectInput(stage_a, 0, input_queue);
    ConnectOutput(stage_a, 0, link_queue);
    ConnectInput(stage_b, 0, link_queue);
    ConnectOutput(stage_b, 0, output_queue);
  }

  void ResetSelf() override {
    ++reset_count;
  }
  void ReportSelf() override {
    ++report_count;
  }
  void XferSelf() override {
    ++xfer_count;
  }

  void WorkSelf() override {
    ++work_count;

    if (build_children_) {
      return;
    }

    INPUT(input, 0);
    OUTPUT(output, 0);
    while (!input->Empty() && !output->Full()) {
      PacketPtr packet = input->Read();
      ++packet->hops;
      output->Write(std::move(packet));
    }
  }

private:
  bool build_children_ = false;
};

} // namespace

int main() {
  ChainModule root(true);
  root.Build();

  if (!root.IsBuilt()) {
    return 1;
  }
  if (root.SubmoduleCount() != 2 || root.OwnedQueueCount() != 3) {
    return 2;
  }
  if (root.InputPortInfo(0).name != "req_in" || root.OutputPortInfo(0).name != "req_out") {
    return 3;
  }

  PacketPtr packet = std::make_unique<Packet>(11);
  Packet *raw = packet.get();
  root.Input(0)->Write(std::move(packet));

  root.Work();
  if (!root.OwnedQueue(1)->Empty()) {
    return 4;
  }
  if (root.ActiveSubmoduleCount() != 0) {
    return 41;
  }

  root.Work();
  if (root.OwnedQueue(1)->Empty()) {
    return 5;
  }
  if (root.ActiveSubmoduleCount() != 1) {
    return 51;
  }

  root.Work();
  if (root.Output(0)->Empty()) {
    return 6;
  }
  if (root.Output(0)->Front().get() != raw) {
    return 7;
  }
  if (root.Output(0)->Front()->value != 11 || root.Output(0)->Front()->hops != 2) {
    return 8;
  }
  if (root.Submodule(0).work_count != 1 || root.Submodule(1).work_count != 1) {
    return 81;
  }

  root.Xfer();
  root.Report();
  root.Reset();

  if (root.reset_count != 1 || root.report_count != 1 || root.xfer_count != 1) {
    return 9;
  }
  if (root.Submodule(0).xfer_count != 0 || root.Submodule(1).xfer_count != 1) {
    return 10;
  }

  return 0;
}
