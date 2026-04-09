#pragma once

#include <source_location>
#include <string_view>

#include "linx/model/logging.hpp"

namespace linx::model::detail {

[[noreturn]] void AssertFail(std::string_view expr, std::string_view message,
                             std::source_location location = std::source_location::current());

} // namespace linx::model::detail

#define LINX_MODEL_ASSERT(expr)                                                                    \
  do {                                                                                             \
    if (!(expr)) {                                                                                 \
      ::linx::model::detail::AssertFail(#expr, std::string_view{},                                 \
                                        std::source_location::current());                          \
    }                                                                                              \
  } while (false)

#define LINX_MODEL_ASSERT_MSG(expr, message)                                                       \
  do {                                                                                             \
    if (!(expr)) {                                                                                 \
      ::linx::model::detail::AssertFail(#expr, (message), std::source_location::current());        \
    }                                                                                              \
  } while (false)
