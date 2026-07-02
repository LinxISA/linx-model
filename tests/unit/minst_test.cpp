#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "linx/model.hpp"

namespace {

using linx::model::DumpString;
using linx::model::SimQueue;
using linx::model::isa::AllMinstForms;
using linx::model::isa::ConstraintsFor;
using linx::model::isa::DecodeMinst;
using linx::model::isa::DecodeMinstPacked;
using linx::model::isa::EncodeMinst;
using linx::model::isa::FieldsFor;
using linx::model::isa::LookupFormByMnemonic;
using linx::model::isa::Minst;
using linx::model::isa::MinstCodecStatus;
using linx::model::isa::MinstConstraintDesc;
using linx::model::isa::MinstConstraintOp;
using linx::model::isa::MinstFieldDesc;
using linx::model::isa::MinstFormDesc;
using linx::model::isa::MinstPtr;
using linx::model::isa::MinstStage;

bool Evaluate(MinstConstraintOp op, std::int64_t lhs, std::int64_t rhs) {
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

std::string ToLower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return out;
}

bool FitsField(const MinstFieldDesc &field, std::int64_t value) {
  if (field.bit_width == 0) {
    return value == 0;
  }
  if (field.signed_hint > 0) {
    if (field.bit_width >= 63) {
      return true;
    }
    const std::int64_t min = -(std::int64_t{1} << (field.bit_width - 1));
    const std::int64_t max = (std::int64_t{1} << (field.bit_width - 1)) - 1;
    return value >= min && value <= max;
  }
  if (value < 0) {
    return false;
  }
  if (field.bit_width >= 64) {
    return true;
  }
  return static_cast<std::uint64_t>(value) <= ((std::uint64_t{1} << field.bit_width) - 1);
}

std::int64_t PickValueForConstraint(const MinstFieldDesc &field,
                                    const MinstConstraintDesc &constraint, bool satisfy) {
  const std::array<std::int64_t, 10> candidates = {
      0, 1, 2, 3, 4, -1, constraint.value, constraint.value - 1, constraint.value + 1, 31,
  };

  for (const auto candidate : candidates) {
    if (!FitsField(field, candidate)) {
      continue;
    }
    if (Evaluate(constraint.op, candidate, constraint.value) == satisfy) {
      return candidate;
    }
  }
  return satisfy ? 0 : constraint.value;
}

std::int64_t PickValueForField(const MinstFieldDesc &field,
                               std::span<const MinstConstraintDesc> constraints) {
  std::vector<std::int64_t> candidates = {
      0, 1, 2, 3, 4, 7, 8, 15, 16, 31, -1,
  };

  if (field.signed_hint > 0 && field.bit_width > 0U && field.bit_width < 63U) {
    candidates.push_back(-(std::int64_t{1} << (field.bit_width - 1)));
    candidates.push_back((std::int64_t{1} << (field.bit_width - 1)) - 1);
  } else if (field.bit_width > 0U && field.bit_width < 63U) {
    candidates.push_back(static_cast<std::int64_t>((std::uint64_t{1} << field.bit_width) - 1U));
  }

  for (const auto &constraint : constraints) {
    if (constraint.field_name != field.name) {
      continue;
    }
    candidates.push_back(constraint.value);
    candidates.push_back(constraint.value - 1);
    candidates.push_back(constraint.value + 1);
  }

  for (const auto candidate : candidates) {
    if (!FitsField(field, candidate)) {
      continue;
    }

    bool valid = true;
    for (const auto &constraint : constraints) {
      if (constraint.field_name != field.name) {
        continue;
      }
      if (!Evaluate(constraint.op, candidate, constraint.value)) {
        valid = false;
        break;
      }
    }

    if (valid) {
      return candidate;
    }
  }

  return 0;
}

Minst BuildSatisfyingInst(const MinstFormDesc &form) {
  Minst inst;
  inst.SetForm(&form);
  inst.MarkStage(MinstStage::Decode);

  const auto constraints = ConstraintsFor(form);
  for (const auto &field : FieldsFor(form)) {
    inst.SetDecodedField(field.name, PickValueForField(field, constraints), field.signed_hint > 0,
                         field.bit_width);
  }

  inst.RebuildTypedViews();
  return inst;
}

int RunRepresentativeRoundTripSmoke() {
  for (const auto mnemonic : {"C.ADD", "ADD", "HL.ADDI", "V.ADD"}) {
    const auto *form = LookupFormByMnemonic(mnemonic);
    if (form == nullptr) {
      return 1;
    }

    Minst inst = BuildSatisfyingInst(*form);
    const auto encoded = EncodeMinst(inst);
    if (!encoded.valid || encoded.status != MinstCodecStatus::Ok) {
      return 2;
    }
    if (encoded.length_bits != form->length_bits) {
      return 3;
    }

    Minst decoded;
    if (DecodeMinstPacked(encoded.bits, encoded.length_bits, decoded) != MinstCodecStatus::Ok) {
      return 4;
    }
    if (encoded.length_bits > 32) {
      Minst split_decoded;
      if (DecodeMinst(encoded.bits & 0xffffffffULL, encoded.bits >> 32U, encoded.length_bits,
                      split_decoded) != MinstCodecStatus::Ok) {
        return 7;
      }
      if (split_decoded.form_id != form->uid) {
        return 8;
      }
    }
    if (decoded.form_id != form->uid || decoded.mnemonic != form->mnemonic) {
      return 5;
    }
    if (decoded.Assemble().find(ToLower(form->mnemonic)) == std::string::npos) {
      std::cerr << "mnemonic mismatch: expected token=" << ToLower(form->mnemonic)
                << " asm=" << decoded.Assemble() << '\n';
      return 6;
    }
  }

  return 0;
}

int RunConstraintViolationSmoke() {
  const auto *form = LookupFormByMnemonic("ADDTPC");
  if (form == nullptr) {
    return 10;
  }
  const auto constraints = ConstraintsFor(*form);
  if (constraints.empty()) {
    return 11;
  }

  Minst inst = BuildSatisfyingInst(*form);
  for (const auto &field : FieldsFor(*form)) {
    if (field.name == constraints.front().field_name) {
      inst.SetDecodedField(field.name, PickValueForConstraint(field, constraints.front(), false),
                           field.signed_hint > 0, field.bit_width);
    }
  }

  const auto encoded = EncodeMinst(inst);
  if (encoded.status != MinstCodecStatus::ConstraintViolation || encoded.valid) {
    return 12;
  }

  return 0;
}

int RunCoverageRoundTripSmoke() {
  const auto forms = AllMinstForms();
  if (forms.size() != 740) {
    return 20;
  }

  for (const auto &form : forms) {
    if (form.uid.empty() || form.mnemonic.empty() || form.encoding_kind.empty()) {
      return 21;
    }

    Minst inst = BuildSatisfyingInst(form);
    const auto encoded = EncodeMinst(inst);
    if (!encoded.valid || encoded.status != MinstCodecStatus::Ok) {
      return 22;
    }

    Minst decoded;
    const auto decode_status = DecodeMinstPacked(encoded.bits, encoded.length_bits, decoded);
    if (decode_status != MinstCodecStatus::Ok) {
      return 23;
    }
    const auto reencoded = EncodeMinst(decoded);
    if (!reencoded.valid || reencoded.bits != encoded.bits ||
        reencoded.length_bits != encoded.length_bits) {
      return 24;
    }
  }

  return 0;
}

int RunDumpAndQueueSmoke() {
  const auto *form = LookupFormByMnemonic("ADD");
  if (form == nullptr) {
    return 30;
  }

  Minst inst = BuildSatisfyingInst(*form);
  inst.uid = 77;
  inst.pc = 0x100;
  const auto encoded = EncodeMinst(inst);
  if (!encoded.valid) {
    return 31;
  }

  auto packet = Minst::CreateFetch(77, 0x100, encoded.bits,
                                   static_cast<std::uint8_t>(encoded.length_bits / 8));
  if (DecodeMinstPacked(encoded.bits, encoded.length_bits, *packet) != MinstCodecStatus::Ok) {
    return 32;
  }

  const std::string dump = DumpString(*packet);
  if (dump.find("form=") == std::string::npos || dump.find("asm=") == std::string::npos ||
      dump.find("fields=") == std::string::npos) {
    return 33;
  }

  SimQueue<MinstPtr> queue(4, 1, "minst_queue");
  Minst *raw = packet.get();
  queue.Write(std::move(packet));
  queue.Work();
  if (queue.Empty() || queue.Front().get() != raw) {
    return 34;
  }

  auto retired = queue.Read();
  retired->MarkRetired();
  if (!retired->IsTerminal()) {
    return 35;
  }

  return 0;
}

int RunRegSrcOperandRebuildSmoke() {
  const auto *form = LookupFormByMnemonic("MCOPY");
  if (form == nullptr) {
    return 40;
  }

  Minst inst;
  inst.SetForm(form);
  inst.MarkStage(MinstStage::Decode);
  inst.SetDecodedField("RegSrc0=DstAddr", 7, false, 5);
  inst.SetDecodedField("RegSrc1=SrcAddr", 8, false, 5);
  inst.SetDecodedField("RegSrc2=Size", 9, false, 5);
  inst.RebuildTypedViews();

  if (inst.srcs.size() != 3) {
    return 41;
  }
  if (inst.srcs[0].value != 7 || inst.srcs[0].field_name != "RegSrc0=DstAddr") {
    return 42;
  }
  if (inst.srcs[1].value != 8 || inst.srcs[1].field_name != "RegSrc1=SrcAddr") {
    return 43;
  }
  if (inst.srcs[2].value != 9 || inst.srcs[2].field_name != "RegSrc2=Size") {
    return 44;
  }
  if (!inst.dsts.empty()) {
    return 45;
  }

  return 0;
}

int RunAsmTemplateReplacementSmoke() {
  {
    Minst inst;
    const auto *form = LookupFormByMnemonic("LUI");
    if (form == nullptr) {
      return 50;
    }
    inst.SetForm(form);
    inst.MarkStage(MinstStage::Decode);
    inst.SetDecodedField("RegDst", 2, false, 5);
    inst.SetDecodedField("imm20", 32, false, 20);
    inst.RebuildTypedViews();
    if (inst.Assemble() != "lui 32, ->{t, u, a0}") {
      return 51;
    }
  }

  {
    Minst inst;
    const auto *form = LookupFormByMnemonic("V.ADD");
    if (form == nullptr) {
      return 52;
    }
    inst.SetForm(form);
    inst.MarkStage(MinstStage::Decode);
    inst.SetDecodedField("SrcL", 3, false, 5);
    inst.SetDecodedField("SrcR", 4, false, 5);
    inst.SetDecodedField("RegDst", 5, false, 5);
    inst.RebuildTypedViews();
    if (inst.Assemble().find("->a3") == std::string::npos) {
      return 53;
    }
  }

  return 0;
}

} // namespace

int main() {
  const auto representative = RunRepresentativeRoundTripSmoke();
  if (representative != 0) {
    std::cerr << "RunRepresentativeRoundTripSmoke failed with code " << representative << '\n';
    return 1;
  }
  const auto constraints = RunConstraintViolationSmoke();
  if (constraints != 0) {
    std::cerr << "RunConstraintViolationSmoke failed with code " << constraints << '\n';
    return 2;
  }
  const auto coverage = RunCoverageRoundTripSmoke();
  if (coverage != 0) {
    std::cerr << "RunCoverageRoundTripSmoke failed with code " << coverage << '\n';
    return 3;
  }
  const auto dump = RunDumpAndQueueSmoke();
  if (dump != 0) {
    std::cerr << "RunDumpAndQueueSmoke failed with code " << dump << '\n';
    return 4;
  }
  const auto operands = RunRegSrcOperandRebuildSmoke();
  if (operands != 0) {
    std::cerr << "RunRegSrcOperandRebuildSmoke failed with code " << operands << '\n';
    return 5;
  }
  const auto asm_replace = RunAsmTemplateReplacementSmoke();
  if (asm_replace != 0) {
    std::cerr << "RunAsmTemplateReplacementSmoke failed with code " << asm_replace << '\n';
    return 6;
  }
  return 0;
}
