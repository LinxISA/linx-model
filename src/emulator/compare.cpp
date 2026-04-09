#include "linx/model/emulator/compare.hpp"

namespace linx::model::emulator {

void CompareHarness::PushWindow(std::deque<MinstRecord> &window, const MinstRecord &record) {
  window.push_back(record);
  while (window.size() > window_size_) {
    window.pop_front();
  }
}

bool CompareHarness::Push(const MinstRecord &ref, const MinstRecord &ca, const LinxState &ref_state,
                          const LinxState &ca_state) {
  if (mismatch_.has_value()) {
    return false;
  }
  PushWindow(ref_window_, ref);
  PushWindow(ca_window_, ca);
  std::string why;
  if (EqualMinstRecord(ref, ca, &why)) {
    return true;
  }
  mismatch_ = CompareMismatch{
      .reason = std::move(why),
      .ref_state = DumpStateSummary(ref_state),
      .ca_state = DumpStateSummary(ca_state),
      .ref_window = ref_window_,
      .ca_window = ca_window_,
  };
  return false;
}

} // namespace linx::model::emulator
