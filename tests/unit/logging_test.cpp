#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "linx/model.hpp"

namespace {

struct Packet {
  std::uint64_t pc = 0;
  bool valid = false;
  int tag = 0;

  void DumpFields(linx::model::PacketDumpWriter &writer) const {
    writer.Field("pc", pc);
    writer.Field("valid", valid);
    writer.Field("tag", tag);
  }
};

class NamedModule : public linx::model::SimObject {
public:
  NamedModule() : linx::model::SimObject("fetch") {}
};

int RunPacketDumpSmoke() {
  const Packet packet{.pc = 16, .valid = true, .tag = 7};
  if (linx::model::DumpString(packet) != "{pc=16, valid=true, tag=7}") {
    return 1;
  }

  std::unique_ptr<Packet> unique = std::make_unique<Packet>(packet);
  if (linx::model::DumpString(unique) != "{pc=16, valid=true, tag=7}") {
    return 2;
  }

  return 0;
}

int RunLoggerSmoke() {
  std::ostringstream sink;
  linx::model::SimSystem sim;
  NamedModule module;

  sim.Logger().SetSink(sink);
  sim.Logger().SetMinLevel(linx::model::LogLevel::Debug);
  sim.AddModule(module);
  sim.Build();
  sim.Reset();
  sim.Step();

  module.Log(linx::model::LogLevel::Info, "s1")
      << "packet=" << Packet{.pc = 32, .valid = true, .tag = 3} << " accepted=" << true;

  const std::string text = sink.str();
  if (text.find("[INFO ] cycle=1 module=fetch stage=s1 | packet={pc=32, valid=true, tag=3} "
                "accepted=true") == std::string::npos) {
    return 10;
  }

  sink.str("");
  sink.clear();
  sim.Logger().SetMinLevel(linx::model::LogLevel::Error);
  module.Log(linx::model::LogLevel::Info, "s2") << "suppressed";
  if (!sink.str().empty()) {
    return 11;
  }

  return 0;
}

int RunAssertSmoke() {
  std::ostringstream sink;
  auto &logger = linx::model::DefaultLogger();
  logger.SetSink(sink);
  logger.SetMinLevel(linx::model::LogLevel::Trace);

  bool threw = false;
  try {
    LINX_MODEL_ASSERT_MSG(false, "broken invariant");
  } catch (const std::logic_error &) {
    threw = true;
  }

  if (!threw) {
    return 20;
  }

  const std::string text = sink.str();
  if (text.find("[FATAL] cycle=? module=assert stage=check | assertion failed: (false) broken "
                "invariant") == std::string::npos) {
    return 21;
  }
  if (text.find("logging_test.cpp") == std::string::npos) {
    return 22;
  }

  return 0;
}

} // namespace

int main() {
  if (RunPacketDumpSmoke() != 0) {
    return 1;
  }
  if (RunLoggerSmoke() != 0) {
    return 2;
  }
  if (RunAssertSmoke() != 0) {
    return 3;
  }
  return 0;
}
