#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "linx/model/isa/minst.hpp"

namespace linx::model::isa {

enum class MinstConstraintOp : std::uint8_t {
  Eq = 0,
  Ne,
  Lt,
  Le,
  Gt,
  Ge,
};

struct MinstFieldPieceDesc {
  std::uint8_t insn_lsb = 0;
  std::uint8_t width = 0;
  std::uint8_t value_lsb = 0;
};

struct MinstFieldDesc {
  std::string_view name;
  std::int8_t signed_hint = -1;
  std::uint16_t bit_width = 0;
  std::uint32_t piece_start = 0;
  std::uint16_t piece_count = 0;
};

struct MinstConstraintDesc {
  std::string_view field_name;
  MinstConstraintOp op = MinstConstraintOp::Eq;
  std::int64_t value = 0;
  std::string_view value_raw;
};

struct MinstFormDesc {
  std::string_view uid;
  std::string_view mnemonic;
  std::string_view asm_template;
  std::string_view encoding_kind;
  std::string_view group;
  std::string_view uop_group;
  std::string_view uop_big_kind;
  std::uint16_t length_bits = 0;
  std::uint16_t fixed_bits = 0;
  std::uint64_t mask = 0;
  std::uint64_t match = 0;
  std::uint32_t field_start = 0;
  std::uint16_t field_count = 0;
  std::uint32_t constraint_start = 0;
  std::uint8_t constraint_count = 0;
};

[[nodiscard]] std::string_view ToString(MinstConstraintOp op) noexcept;

[[nodiscard]] std::span<const MinstFormDesc> AllMinstForms() noexcept;
[[nodiscard]] const MinstFormDesc *LookupFormByUid(std::string_view uid) noexcept;
[[nodiscard]] const MinstFormDesc *LookupFormByMnemonic(std::string_view mnemonic) noexcept;
[[nodiscard]] std::span<const MinstFieldDesc> FieldsFor(const MinstFormDesc &form) noexcept;
[[nodiscard]] std::span<const MinstConstraintDesc>
ConstraintsFor(const MinstFormDesc &form) noexcept;
[[nodiscard]] std::span<const MinstFieldPieceDesc> PiecesFor(const MinstFieldDesc &field) noexcept;

MinstCodecStatus DecodeMinst(std::uint64_t raw_lo, std::uint64_t raw_hi_or_packed, int length_bits,
                             Minst &out) noexcept;
MinstCodecStatus DecodeMinstPacked(std::uint64_t packed_bits, int length_bits, Minst &out) noexcept;
MinstEncodedWord EncodeMinst(const Minst &inst) noexcept;

} // namespace linx::model::isa
