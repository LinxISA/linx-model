#pragma once

#include "linx/model/sim_assert.hpp"

#define LINX_MODEL_INPUT(name, idx)                                                                \
  auto *name [[maybe_unused]] = this->Input(idx);                                                  \
  LINX_MODEL_ASSERT((name) != nullptr)

#define LINX_MODEL_OUTPUT(name, idx)                                                               \
  auto *name [[maybe_unused]] = this->Output(idx);                                                 \
  LINX_MODEL_ASSERT((name) != nullptr)

#define LINX_MODEL_INNER(name, idx)                                                                \
  auto *name [[maybe_unused]] = this->Inner(idx);                                                  \
  LINX_MODEL_ASSERT((name) != nullptr)

#ifndef INPUT
#define INPUT(name, idx) LINX_MODEL_INPUT(name, idx)
#endif

#ifndef OUTPUT
#define OUTPUT(name, idx) LINX_MODEL_OUTPUT(name, idx)
#endif

#ifndef INNER
#define INNER(name, idx) LINX_MODEL_INNER(name, idx)
#endif
