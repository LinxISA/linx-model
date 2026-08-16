#include "linx/model.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>

namespace {

using linx::model::LoadProgramImageFromFile;
using linx::model::RunSimMain;
using linx::model::SimMainArgs;
using linx::model::detail::ParseSimMainArgs;
using linx::model::emulator::CompareHarness;
using linx::model::emulator::DumpStateSummary;
using linx::model::emulator::ExecutionContext;
using linx::model::emulator::ExecutorBackedSim;
using linx::model::emulator::ReferenceExecutor;
using linx::model::emulator::WriteMinstRecordDump;
using linx::model::emulator::WriteMinstRecordJson;

int WriteResultDump(const SimMainArgs &args, const ExecutionContext &ctx, std::ostream &err) {
  if (!args.result_dump_path.has_value()) {
    return 0;
  }
  if (*args.result_size > std::numeric_limits<std::size_t>::max()) {
    err << "result dump size exceeds host limits\n";
    return 1;
  }
  const auto bytes =
      ctx.ReadMemoryRange(*args.result_address, static_cast<std::size_t>(*args.result_size));
  if (!bytes.has_value()) {
    err << "result memory range is not fully mapped\n";
    return 1;
  }
  const std::filesystem::path path(*args.result_dump_path);
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(bytes->data()),
               static_cast<std::streamsize>(bytes->size()));
  if (!output.good()) {
    err << "failed to write result memory: " << path << '\n';
    return 1;
  }
  return 0;
}

int RunReferenceEngine(const SimMainArgs &args, std::ostream &out, std::ostream &err) {
  auto ctx = std::make_shared<ExecutionContext>();
  auto executor = std::make_shared<ReferenceExecutor>(ctx);
  if (args.program_path.has_value()) {
    ctx->LoadProgram(LoadProgramImageFromFile(*args.program_path, args.raw_base_address));
  }
  if (args.enable_disassembly) {
    const auto image = LoadProgramImageFromFile(*args.program_path, args.raw_base_address);
    linx::model::isa::PrintDisassembly(out, image);
  }
  if (args.disassembly_only) {
    return 0;
  }
  if (args.emit_trace_path.has_value()) {
    ctx->SetTracePath(*args.emit_trace_path);
  }
  executor->Run(args.stop_pc, args.max_cycles);
  const int exit_code = ctx->ExitCode();
  return exit_code == 0 ? WriteResultDump(args, *ctx, err) : exit_code;
}

void WriteMismatchDump(const SimMainArgs &args,
                       const linx::model::emulator::CompareMismatch &mismatch) {
  const std::filesystem::path base =
      args.emit_trace_path.has_value() ? std::filesystem::path(*args.emit_trace_path).parent_path()
                                       : std::filesystem::temp_directory_path();
  const auto dump_dir = base / "linx_model_compare_dfx";
  std::filesystem::create_directories(dump_dir);
  {
    std::ofstream out(dump_dir / "mismatch.txt", std::ios::out | std::ios::trunc);
    out << mismatch.reason << '\n';
    out << "ref_state=" << mismatch.ref_state << '\n';
    out << "ca_state=" << mismatch.ca_state << '\n';
  }
  {
    std::ofstream out(dump_dir / "ref_window.jsonl", std::ios::out | std::ios::trunc);
    for (const auto &record : mismatch.ref_window) {
      WriteMinstRecordJson(out, record);
      out << '\n';
    }
  }
  {
    std::ofstream out(dump_dir / "ca_window.jsonl", std::ios::out | std::ios::trunc);
    for (const auto &record : mismatch.ca_window) {
      WriteMinstRecordJson(out, record);
      out << '\n';
    }
  }
  {
    std::ofstream out(dump_dir / "ref_window.dump", std::ios::out | std::ios::trunc);
    for (const auto &record : mismatch.ref_window) {
      WriteMinstRecordDump(out, record);
      out << '\n';
    }
  }
  {
    std::ofstream out(dump_dir / "ca_window.dump", std::ios::out | std::ios::trunc);
    for (const auto &record : mismatch.ca_window) {
      WriteMinstRecordDump(out, record);
      out << '\n';
    }
  }
}

int RunCompareEngine(const SimMainArgs &args, std::ostream &, std::ostream &err) {
  auto ref_ctx = std::make_shared<ExecutionContext>();
  auto ref_exec = std::make_shared<ReferenceExecutor>(ref_ctx);
  auto ca_sim = std::make_shared<ExecutorBackedSim>();

  if (!args.program_path.has_value()) {
    err << "compare engine requires --bin\n";
    return 1;
  }

  const auto image = LoadProgramImageFromFile(*args.program_path, args.raw_base_address);
  ref_ctx->LoadProgram(image);
  if (args.emit_trace_path.has_value()) {
    const std::filesystem::path base(*args.emit_trace_path);
    ref_ctx->SetTracePath((base.parent_path() / "ref.jsonl").string());
    ca_sim->Context().SetTracePath((base.parent_path() / "ca.jsonl").string());
  }
  ca_sim->LoadProgramImage(image);
  ca_sim->Build();
  ca_sim->Reset();

  CompareHarness harness(args.compare_window);
  while (!ref_ctx->Terminated() && !ca_sim->NeedTerminate()) {
    if (args.max_cycles.has_value() && ref_ctx->Cycle() >= *args.max_cycles) {
      break;
    }
    (void)ref_exec->Step(args.stop_pc);
    ca_sim->RunReference(args.stop_pc);
    ca_sim->step();

    if (!ref_ctx->LastCommitted().has_value() || !ca_sim->Context().LastCommitted().has_value()) {
      break;
    }
    if (!harness.Push(*ref_ctx->LastCommitted(), *ca_sim->Context().LastCommitted(),
                      ref_ctx->State(), ca_sim->Context().State())) {
      WriteMismatchDump(args, *harness.Mismatch());
      err << harness.Mismatch()->reason << '\n';
      return 2;
    }
  }

  if (ref_ctx->Committed().size() != ca_sim->Context().Committed().size()) {
    err << "commit count mismatch: ref=" << ref_ctx->Committed().size()
        << " ca=" << ca_sim->Context().Committed().size() << '\n';
    return 3;
  }
  const int exit_code = std::max(ref_ctx->ExitCode(), ca_sim->Context().ExitCode());
  return exit_code == 0 ? WriteResultDump(args, ca_sim->Context(), err) : exit_code;
}

} // namespace

int main(int argc, char **argv) {
  int exit_code = 0;
  const auto parsed = ParseSimMainArgs(argc, argv, std::cout, std::cerr, exit_code);
  if (!parsed.has_value()) {
    return exit_code;
  }

  if (parsed->engine == "ref") {
    return RunReferenceEngine(*parsed, std::cout, std::cerr);
  }
  if (parsed->engine == "compare") {
    return RunCompareEngine(*parsed, std::cout, std::cerr);
  }

  auto sim = std::make_shared<ExecutorBackedSim>();
  const int result = RunSimMain(*sim, *parsed);
  return result == 0 ? WriteResultDump(*parsed, sim->Context(), std::cerr) : result;
}
