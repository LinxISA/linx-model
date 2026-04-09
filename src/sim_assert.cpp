#include "linx/model/sim_assert.hpp"

#include <sstream>
#include <stdexcept>

namespace linx::model::detail {

[[noreturn]] void AssertFail(std::string_view expr, std::string_view message,
                             std::source_location location) {
  std::ostringstream oss;
  oss << "assertion failed: (" << expr << ')';
  if (!message.empty()) {
    oss << ' ' << message;
  }

  DefaultLogger().Emit(LogLevel::Fatal,
                       LogContext{
                           .cycle = std::nullopt,
                           .module = "assert",
                           .stage = "check",
                           .location = location,
                       },
                       oss.str());

  throw std::logic_error(oss.str());
}

} // namespace linx::model::detail
