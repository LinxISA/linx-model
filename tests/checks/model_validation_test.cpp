#include <memory>
#include <stdexcept>
#include <string>

#include "linx/model.hpp"

namespace {

class ValidLeaf : public linx::model::Module<ValidLeaf, int> {
public:
  ValidLeaf() : linx::model::Module<ValidLeaf, int>("valid_leaf") {
    DescribeInput("req_in", "Incoming request token");
    DescribeOutput("resp_out", "Outgoing response token");
  }
};

class MissingDescriptionLeaf : public linx::model::Module<MissingDescriptionLeaf, int> {
public:
  MissingDescriptionLeaf() : linx::model::Module<MissingDescriptionLeaf, int>("missing_desc") {
    DescribeInput("req_in", "");
    DescribeOutput("resp_out", "Outgoing response token");
  }
};

class MissingOutputLeaf : public linx::model::Module<MissingOutputLeaf, int> {
public:
  MissingOutputLeaf() : linx::model::Module<MissingOutputLeaf, int>("missing_output") {
    DescribeInput("req_in", "Incoming request token");
  }
};

class GlueModule : public linx::model::Module<GlueModule, int> {
public:
  GlueModule() : linx::model::Module<GlueModule, int>("glue") {
    SetRequireIOContract(false);
  }
};

class NonQueueObject : public linx::model::SimObject {
public:
  NonQueueObject() : linx::model::SimObject("non_queue") {}
};

class ConflictModule : public linx::model::Module<ConflictModule, int> {
public:
  ConflictModule() : linx::model::Module<ConflictModule, int>("conflict") {
    DescribeInput("req_in", "Incoming request token");
    DescribeOutput("resp_out", "Outgoing response token");
  }

protected:
  void BuildSelf() override {
    auto *queue = CreateOwnedQueue(4, 0, "conflict_q");
    BindInput(0, queue);
    BindOutput(0, queue);
  }
};

class OneModuleSim : public linx::model::SimSystem {
public:
  template <class ModuleT> ModuleT &AddOne() {
    return EmplaceOwnedModule<ModuleT>();
  }
};

int RunValidModelCheck() {
  OneModuleSim sim;
  auto &module = sim.AddOne<ValidLeaf>();
  auto *input = module.CreateOwnedQueue(4, 0, "valid_in_q");
  auto *output = module.CreateOwnedQueue(4, 0, "valid_out_q");
  module.BindInput(0, input);
  module.BindOutput(0, output);
  sim.Build();

  const auto report = sim.Validate();
  if (!report.Ok()) {
    return 1;
  }

  return 0;
}

int RunMissingDescriptionCheck() {
  OneModuleSim sim;
  auto &module = sim.AddOne<MissingDescriptionLeaf>();
  module.BindInput(0, module.CreateOwnedQueue(4, 0, "desc_in_q"));
  module.BindOutput(0, module.CreateOwnedQueue(4, 0, "desc_out_q"));
  sim.Build();

  const auto report = sim.Validate();
  if (!report.HasErrors()) {
    return 10;
  }
  if (report.Format().find("missing a description") == std::string::npos) {
    return 11;
  }

  return 0;
}

int RunMissingOutputCheck() {
  OneModuleSim sim;
  auto &module = sim.AddOne<MissingOutputLeaf>();
  module.BindInput(0, module.CreateOwnedQueue(4, 0, "incomplete_in_q"));
  sim.Build();

  const auto report = sim.Validate();
  if (!report.HasErrors()) {
    return 20;
  }
  if (report.Format().find("at least one input and one output") == std::string::npos) {
    return 21;
  }

  return 0;
}

int RunGlueModuleCheck() {
  OneModuleSim sim;
  sim.AddOne<GlueModule>();
  sim.Build();

  const auto report = sim.Validate();
  if (!report.Ok()) {
    return 30;
  }

  return 0;
}

int RunConflictCheck() {
  bool threw = false;
  try {
    ConflictModule module;
    module.Build();
  } catch (const std::logic_error &) {
    threw = true;
  }

  if (!threw) {
    return 40;
  }

  return 0;
}

int RunNonQueueCheck() {
  OneModuleSim sim;
  NonQueueObject object;
  sim.AddModule(object);
  sim.Build();

  const auto report = sim.Validate();
  if (!report.HasErrors()) {
    return 50;
  }
  if (report.Format().find("not declared as SimQueue-based") == std::string::npos) {
    return 51;
  }

  return 0;
}

} // namespace

int main() {
  if (RunValidModelCheck() != 0) {
    return 1;
  }
  if (RunMissingDescriptionCheck() != 0) {
    return 2;
  }
  if (RunMissingOutputCheck() != 0) {
    return 3;
  }
  if (RunGlueModuleCheck() != 0) {
    return 4;
  }
  if (RunConflictCheck() != 0) {
    return 5;
  }
  if (RunNonQueueCheck() != 0) {
    return 6;
  }
  return 0;
}
