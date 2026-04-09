#pragma once

#include <deque>
#include <optional>
#include <string>

#include "linx/model/emulator/execution_context.hpp"

namespace linx::model::emulator {

struct CompareMismatch {
  std::string reason;
  std::string ref_state;
  std::string ca_state;
  std::deque<MinstRecord> ref_window;
  std::deque<MinstRecord> ca_window;
};

class CompareHarness {
public:
  explicit CompareHarness(std::size_t window_size = 128) : window_size_(window_size) {}

  [[nodiscard]] bool Push(const MinstRecord &ref, const MinstRecord &ca, const LinxState &ref_state,
                          const LinxState &ca_state);
  [[nodiscard]] const std::optional<CompareMismatch> &Mismatch() const noexcept {
    return mismatch_;
  }

private:
  void PushWindow(std::deque<MinstRecord> &window, const MinstRecord &record);

  std::size_t window_size_ = 128;
  std::deque<MinstRecord> ref_window_;
  std::deque<MinstRecord> ca_window_;
  std::optional<CompareMismatch> mismatch_;
};

} // namespace linx::model::emulator
