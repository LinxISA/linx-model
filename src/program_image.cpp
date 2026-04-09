#include "linx/model/program_image.hpp"

namespace linx::model {

std::size_t ProgramImage::ExecutableSectionCount() const noexcept {
  std::size_t count = 0;
  for (const auto &section : sections) {
    if (section.executable) {
      ++count;
    }
  }
  return count;
}

} // namespace linx::model
