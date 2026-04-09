#include "linx/model/sim_main.hpp"

#include <stdexcept>
#include <string>

namespace linx::model::detail {

std::uint64_t ParseUnsignedArg(std::string_view text) {
  std::size_t consumed = 0;
  const std::string owned(text);
  const auto value = std::stoull(owned, &consumed, 0);
  LINX_MODEL_ASSERT(consumed == owned.size());
  return value;
}

void PrintUsage(std::ostream &os, const char *program) {
  os << "Usage: " << program << " [options]\n"
     << "  --engine <kind>     ref|ca|compare\n"
     << "  --bin <path>        Load ELF or raw binary image\n"
     << "  --raw-base <addr>   Base address for raw non-ELF binaries\n"
     << "  --emit-minst-trace <path>\n"
     << "                      Emit Minst-native JSONL trace records\n"
     << "  --disasm            Print program disassembly before simulation\n"
     << "  --disasm-only       Print program disassembly and exit\n"
     << "  --compare-window <n> Buffered compare window size (default 128)\n"
     << "  --stop-pc <pc>      Stop/reference PC in hex or decimal\n"
     << "  --max-cycles <n>    Terminate after n cycles\n"
     << "  --no-reference      Skip RunReference() in the main loop\n"
     << "  --no-pipeview       Skip PrintPipeView() in the main loop\n"
     << "  --no-report         Skip Report() at shutdown\n"
     << "  --log-level <lvl>   trace|debug|info|warn|error|fatal\n"
     << "  -h, --help          Show this help text\n";
}

std::optional<SimMainArgs> ParseSimMainArgs(int argc, char **argv, std::ostream &out,
                                            std::ostream &err, int &exit_code) {
  SimMainArgs args;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);

    if (Matches(arg, "-h") || Matches(arg, "--help")) {
      PrintUsage(out, argv[0]);
      exit_code = 0;
      return std::nullopt;
    }

    if (Matches(arg, "--stop-pc")) {
      if (i + 1 >= argc) {
        err << "missing value for --stop-pc\n";
        exit_code = 1;
        return std::nullopt;
      }
      try {
        args.stop_pc = ParseUnsignedArg(argv[++i]);
      } catch (const std::exception &) {
        err << "invalid integer for --stop-pc: " << argv[i] << '\n';
        exit_code = 1;
        return std::nullopt;
      }
      continue;
    }

    if (Matches(arg, "--engine")) {
      if (i + 1 >= argc) {
        err << "missing value for --engine\n";
        exit_code = 1;
        return std::nullopt;
      }
      args.engine = argv[++i];
      if (args.engine != "ref" && args.engine != "ca" && args.engine != "compare") {
        err << "invalid engine: " << args.engine << '\n';
        exit_code = 1;
        return std::nullopt;
      }
      continue;
    }

    if (Matches(arg, "--bin")) {
      if (i + 1 >= argc) {
        err << "missing value for --bin\n";
        exit_code = 1;
        return std::nullopt;
      }
      args.program_path = std::string(argv[++i]);
      continue;
    }

    if (Matches(arg, "--raw-base")) {
      if (i + 1 >= argc) {
        err << "missing value for --raw-base\n";
        exit_code = 1;
        return std::nullopt;
      }
      try {
        args.raw_base_address = ParseUnsignedArg(argv[++i]);
      } catch (const std::exception &) {
        err << "invalid integer for --raw-base: " << argv[i] << '\n';
        exit_code = 1;
        return std::nullopt;
      }
      continue;
    }

    if (Matches(arg, "--disasm")) {
      args.enable_disassembly = true;
      continue;
    }

    if (Matches(arg, "--disasm-only")) {
      args.enable_disassembly = true;
      args.disassembly_only = true;
      continue;
    }

    if (Matches(arg, "--emit-minst-trace")) {
      if (i + 1 >= argc) {
        err << "missing value for --emit-minst-trace\n";
        exit_code = 1;
        return std::nullopt;
      }
      args.emit_trace_path = std::string(argv[++i]);
      continue;
    }

    if (Matches(arg, "--max-cycles")) {
      if (i + 1 >= argc) {
        err << "missing value for --max-cycles\n";
        exit_code = 1;
        return std::nullopt;
      }
      try {
        args.max_cycles = ParseUnsignedArg(argv[++i]);
      } catch (const std::exception &) {
        err << "invalid integer for --max-cycles: " << argv[i] << '\n';
        exit_code = 1;
        return std::nullopt;
      }
      continue;
    }

    if (Matches(arg, "--compare-window")) {
      if (i + 1 >= argc) {
        err << "missing value for --compare-window\n";
        exit_code = 1;
        return std::nullopt;
      }
      try {
        args.compare_window = static_cast<std::size_t>(ParseUnsignedArg(argv[++i]));
      } catch (const std::exception &) {
        err << "invalid integer for --compare-window: " << argv[i] << '\n';
        exit_code = 1;
        return std::nullopt;
      }
      if (args.compare_window == 0U) {
        err << "--compare-window must be > 0\n";
        exit_code = 1;
        return std::nullopt;
      }
      continue;
    }

    if (Matches(arg, "--no-reference")) {
      args.enable_reference = false;
      continue;
    }

    if (Matches(arg, "--no-pipeview")) {
      args.enable_pipeview = false;
      continue;
    }

    if (Matches(arg, "--no-report")) {
      args.enable_report = false;
      continue;
    }

    if (Matches(arg, "--log-level")) {
      if (i + 1 >= argc) {
        err << "missing value for --log-level\n";
        exit_code = 1;
        return std::nullopt;
      }
      const auto level = ParseLogLevel(argv[++i]);
      if (!level.has_value()) {
        err << "invalid log level: " << argv[i] << '\n';
        exit_code = 1;
        return std::nullopt;
      }
      args.log_level = *level;
      continue;
    }

    if (!arg.empty() && arg.front() != '-') {
      if (args.program_path.has_value()) {
        err << "multiple input binaries provided\n";
        exit_code = 1;
        return std::nullopt;
      }
      args.program_path = std::string(arg);
      continue;
    }

    err << "unknown option: " << arg << '\n';
    exit_code = 1;
    return std::nullopt;
  }

  exit_code = 0;
  return args;
}

} // namespace linx::model::detail
