#include <memory>
#include <type_traits>

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
using SharedPacketPtr = std::shared_ptr<Packet>;

static_assert(
    std::is_same_v<linx::model::SimUniqueQueue<Packet>, linx::model::SimQueue<PacketPtr>>);
static_assert(std::is_same_v<linx::model::SimQueue<int>::value_type, int>);

int RunUniqueQueueSmoke() {
  linx::model::SimUniqueQueue<Packet> queue(4, 1, "uq");
  PacketPtr packet = std::make_unique<Packet>(11);
  Packet *raw = packet.get();

  queue.Write(std::move(packet));
  queue.Work();
  if (queue.Empty()) {
    return 1;
  }
  if (queue.Front().get() != raw) {
    return 2;
  }
  if (queue.Front()->value != 11) {
    return 3;
  }

  PacketPtr result = queue.Read();
  if (result.get() != raw) {
    return 4;
  }

  return 0;
}

int RunSharedQueueSmoke() {
  linx::model::SimSharedQueue<Packet> queue(4, 1, "sq");
  SharedPacketPtr packet = std::make_shared<Packet>(21);
  Packet *raw = packet.get();

  queue.Write(packet);
  if (packet.use_count() != 2) {
    return 10;
  }

  queue.Work();
  if (queue.Empty()) {
    return 11;
  }
  if (queue.Front().get() != raw) {
    return 12;
  }

  SharedPacketPtr result = queue.Read();
  if (result.get() != raw) {
    return 13;
  }
  if (result.use_count() != 2) {
    return 14;
  }

  return 0;
}

int RunValueQueueSmoke() {
  linx::model::SimQueue<int> values(8, 1, "values");
  linx::model::SimQueue<bool> flags(8, 0, "flags");

  values.Write(42);
  flags.Write(true);

  if (!values.Empty() || !flags.Empty()) {
    return 20;
  }

  values.Work();
  flags.Work();

  if (values.Empty() || flags.Empty()) {
    return 21;
  }
  if (values.Read() != 42) {
    return 22;
  }
  if (!flags.Read()) {
    return 23;
  }

  return 0;
}

} // namespace

int main() {
  if (RunUniqueQueueSmoke() != 0) {
    return 1;
  }
  if (RunSharedQueueSmoke() != 0) {
    return 2;
  }
  if (RunValueQueueSmoke() != 0) {
    return 3;
  }
  return 0;
}
