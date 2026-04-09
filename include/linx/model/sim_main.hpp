#pragma once

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "linx/model/logging.hpp"
#include "linx/model/sim_assert.hpp"
#include "linx/model/sim_system.hpp"

namespace linx::model {

/**
 * @brief Parsed command-line options for simulator binaries.
 */
struct SimMainArgs {
  std::optional<std::uint64_t> stop_pc;
  std::optional<std::uint64_t> max_cycles;
  LogLevel log_level = LogLevel::Info;
  bool enable_reference = true;
  bool enable_pipeview = true;
  bool enable_report = true;
};

namespace detail {

std::uint64_t ParseUnsignedArg(std::string_view text);
void PrintUsage(std::ostream &os, const char *program);
inline bool Matches(std::string_view arg, std::string_view expected) {
  return arg == expected;
}
std::optional<SimMainArgs> ParseSimMainArgs(int argc, char **argv, std::ostream &out,
                                            std::ostream &err, int &exit_code);

} // namespace detail

template <class SimT>
int RunSimMain(SimT &sim, const SimMainArgs &args, std::ostream &out = std::cout) {
  if constexpr (requires { sim.Logger(); }) {
    sim.Logger().SetMinLevel(args.log_level);
  }

  sim.Build();
  sim.Reset();

  bool terminate = sim.needTerminate();
  while (!terminate) {
    if (args.enable_reference) {
      sim.RunReference(args.stop_pc);
    }

    sim.step();

    if (args.enable_pipeview) {
      sim.PrintPipeView(out);
    }

    terminate = sim.needTerminate();
    if (!terminate && args.max_cycles.has_value() && sim.Cycle() >= *args.max_cycles) {
      sim.RequestTerminate();
      terminate = true;
    }
  }

  if (args.enable_report) {
    sim.Report();
  }

  return 0;
}

template <class SimT>
int RunSimMain(int argc, char **argv, SimT &sim, std::ostream &out = std::cout,
               std::ostream &err = std::cerr) {
  int exit_code = 0;
  const auto parsed = detail::ParseSimMainArgs(argc, argv, out, err, exit_code);
  if (!parsed.has_value()) {
    return exit_code;
  }
  return RunSimMain(sim, *parsed, out);
}

template <class Factory>
int RunSimMainWithFactory(int argc, char **argv, Factory &&factory, std::ostream &out = std::cout,
                          std::ostream &err = std::cerr) {
  int exit_code = 0;
  const auto parsed = detail::ParseSimMainArgs(argc, argv, out, err, exit_code);
  if (!parsed.has_value()) {
    return exit_code;
  }

  auto sim = std::invoke(std::forward<Factory>(factory), *parsed);
  LINX_MODEL_ASSERT(static_cast<bool>(sim));
  return RunSimMain(*sim, *parsed, out);
}

#define LINX_MODEL_DEFINE_MAIN(factory_expr)                                                       \
  int main(int argc, char **argv) {                                                                \
    return ::linx::model::RunSimMainWithFactory(argc, argv, factory_expr);                         \
  }

} // namespace linx::model
