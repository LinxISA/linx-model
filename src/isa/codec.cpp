#include "linx/model/isa/codec.hpp"

#include <algorithm>
#include <limits>
#include <optional>

#include "linx/model/isa/generated_tables.hpp"

namespace linx::model::isa {

namespace {

using generated::kConstraints;
using generated::kFieldPieces;
using generated::kFields;
using generated::kFormCount;
using generated::kForms;

[[nodiscard]] std::uint64_t BitMask(std::uint16_t width) noexcept {
  if (width == 0) {
    return 0;
  }
  if (width >= 64) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  return (std::uint64_t{1} << width) - 1;
}

[[nodiscard]] std::int64_t SignExtend(std::uint64_t value, std::uint16_t width) noexcept {
  if (width == 0) {
    return 0;
  }
  if (width >= 64) {
    return static_cast<std::int64_t>(value);
  }
  const std::uint64_t sign_bit = std::uint64_t{1} << (width - 1);
  if ((value & sign_bit) == 0) {
    return static_cast<std::int64_t>(value);
  }
  return static_cast<std::int64_t>(value | ~BitMask(width));
}

[[nodiscard]] bool EvaluateConstraint(MinstConstraintOp op, std::int64_t lhs,
                                      std::int64_t rhs) noexcept {
  switch (op) {
  case MinstConstraintOp::Eq:
    return lhs == rhs;
  case MinstConstraintOp::Ne:
    return lhs != rhs;
  case MinstConstraintOp::Lt:
    return lhs < rhs;
  case MinstConstraintOp::Le:
    return lhs <= rhs;
  case MinstConstraintOp::Gt:
    return lhs > rhs;
  case MinstConstraintOp::Ge:
    return lhs >= rhs;
  }
  return false;
}

[[nodiscard]] std::optional<std::int64_t> ExtractFieldValue(const MinstFieldDesc &field,
                                                            std::uint64_t packed_bits) noexcept {
  std::uint64_t raw = 0;
  for (const auto &piece : PiecesFor(field)) {
    const std::uint64_t piece_mask = BitMask(piece.width);
    raw |= ((packed_bits >> piece.insn_lsb) & piece_mask) << piece.value_lsb;
  }

  if (field.signed_hint > 0) {
    return SignExtend(raw, field.bit_width);
  }
  return static_cast<std::int64_t>(raw);
}

[[nodiscard]] bool FitsField(const MinstFieldDesc &field, std::int64_t logical_value) noexcept {
  if (field.bit_width == 0) {
    return logical_value == 0;
  }

  if (field.signed_hint > 0) {
    if (field.bit_width >= 64) {
      return true;
    }
    const std::int64_t min = -(std::int64_t{1} << (field.bit_width - 1));
    const std::int64_t max = (std::int64_t{1} << (field.bit_width - 1)) - 1;
    return logical_value >= min && logical_value <= max;
  }

  if (logical_value < 0) {
    return false;
  }
  return static_cast<std::uint64_t>(logical_value) <= BitMask(field.bit_width);
}

[[nodiscard]] std::uint64_t EncodeFieldBits(const MinstFieldDesc &field,
                                            std::int64_t logical_value) noexcept {
  return static_cast<std::uint64_t>(logical_value) & BitMask(field.bit_width);
}

[[nodiscard]] MinstCodecStatus DecodePackedWord(std::uint64_t packed_bits, int length_bits,
                                                Minst &out) noexcept {
  if (length_bits != 16 && length_bits != 32 && length_bits != 48 && length_bits != 64) {
    out.ClearDecodedState();
    out.SetRawEncoding(packed_bits, 0);
    out.decode_status = MinstCodecStatus::InvalidLength;
    return out.decode_status;
  }

  packed_bits &= BitMask(static_cast<std::uint16_t>(length_bits));
  const MinstFormDesc *best_form = nullptr;
  std::vector<const MinstFormDesc *> valid_matches;
  int best_fixed_bits = -1;
  bool saw_fixed_match = false;

  for (const auto &form : std::span<const MinstFormDesc>{kForms, kFormCount}) {
    if (form.length_bits != static_cast<std::uint16_t>(length_bits)) {
      continue;
    }
    if ((packed_bits & form.mask) != form.match) {
      continue;
    }
    saw_fixed_match = true;

    bool constraints_ok = true;
    for (const auto &constraint : ConstraintsFor(form)) {
      const auto fields = FieldsFor(form);
      const auto field = std::find_if(fields.begin(), fields.end(), [&](const auto &desc) {
        return desc.name == constraint.field_name;
      });
      if (field == fields.end()) {
        constraints_ok = false;
        break;
      }
      const auto value = ExtractFieldValue(*field, packed_bits);
      if (!value.has_value() || !EvaluateConstraint(constraint.op, *value, constraint.value)) {
        constraints_ok = false;
        break;
      }
    }

    if (!constraints_ok) {
      continue;
    }

    if (static_cast<int>(form.fixed_bits) > best_fixed_bits) {
      valid_matches.clear();
      valid_matches.push_back(&form);
      best_form = &form;
      best_fixed_bits = static_cast<int>(form.fixed_bits);
    } else if (static_cast<int>(form.fixed_bits) == best_fixed_bits) {
      valid_matches.push_back(&form);
    }
  }

  out.ClearDecodedState();
  out.SetRawEncoding(packed_bits, static_cast<std::uint8_t>(length_bits));

  if (valid_matches.empty()) {
    out.decode_status =
        saw_fixed_match ? MinstCodecStatus::ConstraintViolation : MinstCodecStatus::NoMatch;
    return out.decode_status;
  }

  if (valid_matches.size() != 1) {
    out.decode_status = MinstCodecStatus::AmbiguousMatch;
    return out.decode_status;
  }

  best_form = valid_matches.front();
  out.SetForm(best_form);
  out.valid_form = true;
  out.decode_status = MinstCodecStatus::Ok;
  out.encode_status = MinstCodecStatus::Ok;
  if (out.stage == MinstStage::Invalid) {
    out.stage = MinstStage::Fetch;
  }
  if (out.lifecycle == MinstLifecycle::Invalid) {
    out.lifecycle = MinstLifecycle::Allocated;
  }

  for (const auto &field : FieldsFor(*best_form)) {
    const auto value = ExtractFieldValue(field, packed_bits);
    if (!value.has_value()) {
      out.decode_status = MinstCodecStatus::NoMatch;
      return out.decode_status;
    }
    out.SetDecodedField(field.name, *value, field.signed_hint > 0, field.bit_width);
  }

  out.RebuildTypedViews();
  return out.decode_status;
}

} // namespace

std::string_view ToString(MinstConstraintOp op) noexcept {
  switch (op) {
  case MinstConstraintOp::Eq:
    return "==";
  case MinstConstraintOp::Ne:
    return "!=";
  case MinstConstraintOp::Lt:
    return "<";
  case MinstConstraintOp::Le:
    return "<=";
  case MinstConstraintOp::Gt:
    return ">";
  case MinstConstraintOp::Ge:
    return ">=";
  }
  return "?";
}

std::span<const MinstFormDesc> AllMinstForms() noexcept {
  return {kForms, generated::kFormCount};
}

const MinstFormDesc *LookupFormByUid(std::string_view uid) noexcept {
  const auto forms = AllMinstForms();
  const auto it =
      std::find_if(forms.begin(), forms.end(), [&](const auto &form) { return form.uid == uid; });
  return it == forms.end() ? nullptr : &(*it);
}

const MinstFormDesc *LookupFormByMnemonic(std::string_view mnemonic) noexcept {
  const auto forms = AllMinstForms();
  const auto it = std::find_if(forms.begin(), forms.end(),
                               [&](const auto &form) { return form.mnemonic == mnemonic; });
  return it == forms.end() ? nullptr : &(*it);
}

std::span<const MinstFieldDesc> FieldsFor(const MinstFormDesc &form) noexcept {
  return {kFields + form.field_start, form.field_count};
}

std::span<const MinstConstraintDesc> ConstraintsFor(const MinstFormDesc &form) noexcept {
  return {kConstraints + form.constraint_start, form.constraint_count};
}

std::span<const MinstFieldPieceDesc> PiecesFor(const MinstFieldDesc &field) noexcept {
  return {kFieldPieces + field.piece_start, field.piece_count};
}

MinstCodecStatus DecodeMinst(std::uint64_t raw_lo, std::uint64_t raw_hi_or_packed, int length_bits,
                             Minst &out) noexcept {
  std::uint64_t packed_bits = raw_lo;
  if (length_bits > 32) {
    packed_bits = (raw_lo & BitMask(32)) | (raw_hi_or_packed << 32U);
  }
  return DecodePackedWord(packed_bits, length_bits, out);
}

MinstCodecStatus DecodeMinstPacked(std::uint64_t packed_bits, int length_bits,
                                   Minst &out) noexcept {
  return DecodePackedWord(packed_bits, length_bits, out);
}

MinstEncodedWord EncodeMinst(const Minst &inst) noexcept {
  MinstEncodedWord out{};
  if (inst.form == nullptr || !inst.valid_form) {
    out.status = MinstCodecStatus::InvalidForm;
    return out;
  }

  std::uint64_t bits = inst.form->match;
  for (const auto &field_desc : FieldsFor(*inst.form)) {
    const auto *field = inst.FindDecodedField(field_desc.name);
    if (field == nullptr) {
      out.status = MinstCodecStatus::MissingField;
      return out;
    }
    if (!FitsField(field_desc, field->logical_value)) {
      out.status = MinstCodecStatus::ValueOutOfRange;
      return out;
    }

    const std::uint64_t raw_value = EncodeFieldBits(field_desc, field->logical_value);
    for (const auto &piece : PiecesFor(field_desc)) {
      const std::uint64_t piece_mask = BitMask(piece.width);
      bits &= ~(piece_mask << piece.insn_lsb);
      bits |= ((raw_value >> piece.value_lsb) & piece_mask) << piece.insn_lsb;
    }
  }

  for (const auto &constraint : ConstraintsFor(*inst.form)) {
    const auto *field = inst.FindDecodedField(constraint.field_name);
    if (field == nullptr) {
      out.status = MinstCodecStatus::MissingField;
      return out;
    }
    if (!EvaluateConstraint(constraint.op, field->logical_value, constraint.value)) {
      out.status = MinstCodecStatus::ConstraintViolation;
      return out;
    }
  }

  out.bits = bits;
  out.length_bits = static_cast<std::uint8_t>(inst.form->length_bits);
  out.bits &= BitMask(out.length_bits);
  out.valid = true;
  out.status = MinstCodecStatus::Ok;
  return out;
}

} // namespace linx::model::isa
