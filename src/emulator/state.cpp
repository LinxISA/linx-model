#include "linx/model/emulator/state.hpp"

#include <sstream>

namespace linx::model::emulator {

std::string DumpStateSummary(const LinxState &state) {
  std::ostringstream oss;
  oss << "{pc=" << state.pc << ",acr=" << state.acr << ",bpc=" << state.bpc << ",tgt=" << state.tgt
      << ",cond=" << state.cond << ",carg=" << state.carg << ",brtype=" << state.brtype
      << ",block_kind=" << state.block_kind << ",lane_id=" << state.lane_id << ",a0=" << state.gpr[2]
      << ",a1=" << state.gpr[3] << ",sp=" << state.gpr[1] << ",ra=" << state.gpr[10]
      << ",insn_count=" << state.insn_count << "}";
  return oss.str();
}

} // namespace linx::model::emulator
