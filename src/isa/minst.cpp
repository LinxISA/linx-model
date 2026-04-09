#include "linx/model/isa/minst.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <span>

#include "linx/model/isa/codec.hpp"

namespace linx::model::isa {

namespace {

[[nodiscard]] MinstWidth WidthFromBits(std::uint16_t bits) noexcept {
  switch (bits) {
  case 8:
    return MinstWidth::B8;
  case 16:
    return MinstWidth::B16;
  case 32:
    return MinstWidth::B32;
  case 64:
    return MinstWidth::B64;
  case 128:
    return MinstWidth::B128;
  default:
    return MinstWidth::None;
  }
}

[[nodiscard]] bool StartsWith(std::string_view value, std::string_view prefix) noexcept {
  return value.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] bool Contains(std::string_view value, std::string_view needle) noexcept {
  return value.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool IsAsmTokenChar(char ch) noexcept {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
         ch == '_' || ch == '#';
}

void ReplaceAsmToken(std::string &text, std::string_view needle, std::string_view replacement) {
  if (needle.empty()) {
    return;
  }
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    const bool left_ok = pos == 0U || !IsAsmTokenChar(text[pos - 1U]);
    const auto right_idx = pos + needle.size();
    const bool right_ok = right_idx >= text.size() || !IsAsmTokenChar(text[right_idx]);
    if (!left_ok || !right_ok) {
      pos += needle.size();
      continue;
    }
    text.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
}

[[nodiscard]] std::string JoinFieldDump(std::span<const MinstDecodedField> fields) {
  std::ostringstream oss;
  for (std::size_t idx = 0; idx < fields.size(); ++idx) {
    if (idx != 0U) {
      oss << ';';
    }
    oss << DumpString(fields[idx]);
  }
  return oss.str();
}

[[nodiscard]] std::string JoinOperandDump(std::span<const MinstOperand> operands) {
  std::ostringstream oss;
  for (std::size_t idx = 0; idx < operands.size(); ++idx) {
    if (idx != 0U) {
      oss << ';';
    }
    oss << DumpString(operands[idx]);
  }
  return oss.str();
}

[[nodiscard]] bool IsDestinationField(std::string_view name) noexcept {
  return StartsWith(name, "RegDst") || StartsWith(name, "Dst") || name == "rd";
}

[[nodiscard]] bool IsSourceField(std::string_view name) noexcept {
  return StartsWith(name, "Src") || StartsWith(name, "RegSrc") || name == "rs1" || name == "rs2" ||
         name == "rs3";
}

[[nodiscard]] bool IsImmediateField(std::string_view name) noexcept {
  return Contains(name, "imm") || name == "shamt" || name == "far" || name == "Mode" ||
         name == "TileOpcode";
}

[[nodiscard]] MinstOperandKind KindFromField(std::string_view name) noexcept {
  if (IsImmediateField(name)) {
    return MinstOperandKind::Immediate;
  }
  if (Contains(name, "Pred") || Contains(name, "pred")) {
    return MinstOperandKind::Predicate;
  }
  if (Contains(name, "Tile") || name == "DataType" || name == "SSR_ID") {
    return MinstOperandKind::Metadata;
  }
  if (Contains(name, "PC") || Contains(name, "far") || Contains(name, "Mode")) {
    return MinstOperandKind::Control;
  }
  return MinstOperandKind::Register;
}

[[nodiscard]] std::string_view RegisterAsmName(std::uint64_t reg) noexcept {
  static constexpr std::array<std::string_view, 32> kNames = {
      "zero", "sp", "a0",  "a1",  "a2",  "a3",  "a4",  "a5",  "a6",  "a7",  "ra",
      "s0",   "s1", "s2",  "s3",  "s4",  "s5",  "s6",  "s7",  "s8",  "x0",  "x1",
      "x2",   "x3", "t#1", "t#2", "t#3", "t#4", "u#1", "u#2", "u#3", "u#4",
  };
  return reg < kNames.size() ? kNames[reg] : "invalid-reg";
}

[[nodiscard]] bool IsAsmRegisterField(std::string_view name) noexcept {
  if (name == "RegDst" || StartsWith(name, "RegDst") || StartsWith(name, "RegSrc")) {
    return true;
  }
  if (StartsWith(name, "Src") || StartsWith(name, "src")) {
    return !Contains(name, "Tile");
  }
  return false;
}

[[nodiscard]] std::string FormatAsmFieldValue(const MinstDecodedField &field) {
  if (IsAsmRegisterField(field.name)) {
    return std::string(RegisterAsmName(field.UnsignedValue()));
  }

  std::ostringstream oss;
  if (field.signed_hint) {
    oss << field.logical_value;
  } else {
    oss << field.UnsignedValue();
  }
  return oss.str();
}

struct AsmReplacement {
  std::string needle;
  std::string replacement;
};

void AddReplacement(std::vector<AsmReplacement> &replacements, std::string_view needle,
                    std::string replacement) {
  if (needle.empty()) {
    return;
  }
  replacements.push_back(AsmReplacement{
      .needle = std::string(needle),
      .replacement = std::move(replacement),
  });
}

void AddImmediateAliases(std::vector<AsmReplacement> &replacements, const MinstDecodedField &field,
                         std::string value, std::size_t immediate_field_count) {
  if (StartsWith(field.name, "uimm")) {
    AddReplacement(replacements, "uimm", value);
  }
  if (StartsWith(field.name, "simm")) {
    AddReplacement(replacements, "simm", value);
  }
  if (StartsWith(field.name, "imm")) {
    AddReplacement(replacements, "imm", value);
  }
  if (immediate_field_count == 1U) {
    AddReplacement(replacements, "imm", value);
    AddReplacement(replacements, "simm", value);
    AddReplacement(replacements, "uimm", value);
  }
}

void AddFieldAliases(std::vector<AsmReplacement> &replacements, const MinstDecodedField &field,
                     std::string value, std::size_t immediate_field_count) {
  AddReplacement(replacements, field.name, value);
  if (const auto suffix = field.name.find('='); suffix != std::string_view::npos) {
    AddReplacement(replacements, field.name.substr(0, suffix), value);
  }

  if (field.name == "RegDst") {
    AddReplacement(replacements, "Rd", value);
    AddReplacement(replacements, "Dst", value);
  }
  if (field.name == "SrcL") {
    AddReplacement(replacements, "srcL", value);
  }
  if (field.name == "SrcR") {
    AddReplacement(replacements, "srcR", value);
  }
  if (field.name == "SrcP") {
    AddReplacement(replacements, "srcP", value);
  }
  if (field.name == "SrcD") {
    AddReplacement(replacements, "srcD", value);
  }

  if (IsImmediateField(field.name)) {
    AddImmediateAliases(replacements, field, std::move(value), immediate_field_count);
  }
}

[[nodiscard]] std::vector<AsmReplacement>
BuildAsmReplacements(std::span<const MinstDecodedField> fields) {
  std::vector<AsmReplacement> replacements;
  replacements.reserve(fields.size() * 4U);
  const auto immediate_field_count = static_cast<std::size_t>(
      std::count_if(fields.begin(), fields.end(),
                    [](const MinstDecodedField &field) { return IsImmediateField(field.name); }));
  for (const auto &field : fields) {
    AddFieldAliases(replacements, field, FormatAsmFieldValue(field), immediate_field_count);
  }
  std::sort(replacements.begin(), replacements.end(),
            [](const auto &lhs, const auto &rhs) { return lhs.needle.size() > rhs.needle.size(); });
  replacements.erase(std::unique(replacements.begin(), replacements.end(),
                                 [](const auto &lhs, const auto &rhs) {
                                   return lhs.needle == rhs.needle &&
                                          lhs.replacement == rhs.replacement;
                                 }),
                     replacements.end());
  return replacements;
}

} // namespace

std::string_view ToString(MinstOperandKind kind) noexcept {
  switch (kind) {
  case MinstOperandKind::Invalid:
    return "invalid";
  case MinstOperandKind::Register:
    return "reg";
  case MinstOperandKind::Immediate:
    return "imm";
  case MinstOperandKind::Predicate:
    return "pred";
  case MinstOperandKind::Memory:
    return "mem";
  case MinstOperandKind::Control:
    return "ctrl";
  case MinstOperandKind::Metadata:
    return "meta";
  }
  return "unknown";
}

std::string_view ToString(MinstWidth width) noexcept {
  switch (width) {
  case MinstWidth::None:
    return "-";
  case MinstWidth::B8:
    return "b8";
  case MinstWidth::B16:
    return "b16";
  case MinstWidth::B32:
    return "b32";
  case MinstWidth::B64:
    return "b64";
  case MinstWidth::B128:
    return "b128";
  }
  return "unknown";
}

std::string_view ToString(MinstOpcodeClass opcode_class) noexcept {
  switch (opcode_class) {
  case MinstOpcodeClass::Invalid:
    return "invalid";
  case MinstOpcodeClass::Nop:
    return "nop";
  case MinstOpcodeClass::Integer:
    return "int";
  case MinstOpcodeClass::FloatingPoint:
    return "fp";
  case MinstOpcodeClass::Branch:
    return "branch";
  case MinstOpcodeClass::Load:
    return "load";
  case MinstOpcodeClass::Store:
    return "store";
  case MinstOpcodeClass::Atomic:
    return "atomic";
  case MinstOpcodeClass::System:
    return "system";
  }
  return "unknown";
}

std::string_view ToString(MinstStage stage) noexcept {
  switch (stage) {
  case MinstStage::Invalid:
    return "invalid";
  case MinstStage::Fetch:
    return "fetch";
  case MinstStage::Decode:
    return "decode";
  case MinstStage::Rename:
    return "rename";
  case MinstStage::Dispatch:
    return "dispatch";
  case MinstStage::Issue:
    return "issue";
  case MinstStage::Execute:
    return "execute";
  case MinstStage::Memory:
    return "memory";
  case MinstStage::Writeback:
    return "writeback";
  case MinstStage::Rob:
    return "rob";
  case MinstStage::Retire:
    return "retire";
  case MinstStage::Flush:
    return "flush";
  case MinstStage::Dfx:
    return "dfx";
  }
  return "unknown";
}

std::string_view ToString(MinstLifecycle lifecycle) noexcept {
  switch (lifecycle) {
  case MinstLifecycle::Invalid:
    return "invalid";
  case MinstLifecycle::Allocated:
    return "allocated";
  case MinstLifecycle::InFlight:
    return "in_flight";
  case MinstLifecycle::Retired:
    return "retired";
  case MinstLifecycle::Flushed:
    return "flushed";
  case MinstLifecycle::Traced:
    return "traced";
  }
  return "unknown";
}

std::string_view ToString(MinstCodecStatus status) noexcept {
  switch (status) {
  case MinstCodecStatus::Ok:
    return "ok";
  case MinstCodecStatus::InvalidLength:
    return "invalid_length";
  case MinstCodecStatus::NoMatch:
    return "no_match";
  case MinstCodecStatus::AmbiguousMatch:
    return "ambiguous_match";
  case MinstCodecStatus::ConstraintViolation:
    return "constraint_violation";
  case MinstCodecStatus::InvalidForm:
    return "invalid_form";
  case MinstCodecStatus::MissingField:
    return "missing_field";
  case MinstCodecStatus::ValueOutOfRange:
    return "value_out_of_range";
  }
  return "unknown";
}

std::uint64_t MinstDecodedField::UnsignedValue() const noexcept {
  if (bit_width == 0) {
    return 0;
  }
  if (bit_width >= 64) {
    return static_cast<std::uint64_t>(logical_value);
  }
  return static_cast<std::uint64_t>(logical_value) & ((std::uint64_t{1} << bit_width) - 1);
}

void MinstDecodedField::DumpFields(::linx::model::PacketDumpWriter &writer) const {
  writer.Field("name", name);
  writer.Field("signed", signed_hint);
  writer.Field("bits", static_cast<unsigned>(bit_width));
  writer.Field("logical", logical_value);
  writer.Field("raw", UnsignedValue());
}

void MinstOperand::DumpFields(::linx::model::PacketDumpWriter &writer) const {
  writer.Field("dst", is_dst);
  writer.Field("ready", ready);
  writer.Field("kind", ToString(kind));
  writer.Field("logical_width", ToString(logical_width));
  writer.Field("encoded_bits", static_cast<unsigned>(encoded_bits));
  writer.Field("value", value);
  writer.Field("data", data);
  writer.Field("field", field_name);
  writer.Field("note", annotation);
}

void MinstMemoryInfo::DumpFields(::linx::model::PacketDumpWriter &writer) const {
  writer.Field("valid", valid);
  writer.Field("load", is_load);
  writer.Field("store", is_store);
  writer.Field("logical_width", ToString(logical_width));
  writer.Field("addr", addr);
  writer.Field("size", size);
  writer.Field("note", annotation);
}

std::string MinstEncodedWord::HexString() const {
  std::ostringstream oss;
  const std::size_t digits = static_cast<std::size_t>((length_bits + 3U) / 4U);
  oss << "0x" << std::hex << std::nouppercase << std::setfill('0') << std::setw(digits) << bits;
  return oss.str();
}

void MinstEncodedWord::DumpFields(::linx::model::PacketDumpWriter &writer) const {
  writer.Field("valid", valid);
  writer.Field("status", ToString(status));
  writer.Field("length_bits", length_bits);
  writer.Field("bits", HexString());
}

Minst::Minst() {
  decoded_fields.reserve(16);
  srcs.reserve(8);
  dsts.reserve(8);
  immediates.reserve(8);
}

Minst::Ptr Minst::CreateFetch(std::uint64_t uid_value, std::uint64_t pc_value,
                              std::uint64_t raw_value, std::uint8_t code_size) {
  auto packet = std::make_unique<Minst>();
  packet->uid = uid_value;
  packet->fetch_seq = uid_value;
  packet->pc = pc_value;
  packet->next_pc = pc_value + code_size;
  packet->SetRawEncoding(raw_value, static_cast<std::uint8_t>(code_size * 8U));
  packet->stage = MinstStage::Fetch;
  packet->lifecycle = MinstLifecycle::Allocated;
  return packet;
}

Minst::SharedPtr Minst::Share(Ptr packet) {
  return SharedPtr(std::move(packet));
}

void Minst::ClearDecodedState() {
  form = nullptr;
  form_id = {};
  mnemonic = {};
  asm_template = {};
  encoding_kind = {};
  group = {};
  uop_group = {};
  uop_big_kind = {};
  decode_status = MinstCodecStatus::NoMatch;
  encode_status = MinstCodecStatus::InvalidForm;
  valid_form = false;
  opcode_class = MinstOpcodeClass::Invalid;
  annotation.clear();
  decoded_fields.clear();
  srcs.clear();
  dsts.clear();
  immediates.clear();
  shift_amount.reset();
  memory.reset();
  is_branch = false;
  is_control = false;
}

void Minst::SetForm(const MinstFormDesc *desc) {
  form = desc;
  valid_form = desc != nullptr;
  if (desc == nullptr) {
    form_id = {};
    mnemonic = {};
    asm_template = {};
    encoding_kind = {};
    group = {};
    uop_group = {};
    uop_big_kind = {};
    return;
  }
  form_id = desc->uid;
  mnemonic = desc->mnemonic;
  asm_template = desc->asm_template;
  encoding_kind = desc->encoding_kind;
  group = desc->group;
  uop_group = desc->uop_group;
  uop_big_kind = desc->uop_big_kind;
  length_bits = static_cast<std::uint8_t>(desc->length_bits);
  code_bytes = static_cast<std::uint8_t>((desc->length_bits + 7U) / 8U);
}

void Minst::SetRawEncoding(std::uint64_t bits_value, std::uint8_t length_bits_value) {
  raw_bits = bits_value;
  length_bits = length_bits_value;
  code_bytes = static_cast<std::uint8_t>((length_bits_value + 7U) / 8U);
}

void Minst::SetDecodedField(std::string_view name, std::int64_t logical_value, bool signed_hint,
                            std::uint16_t bit_width) {
  auto it = std::find_if(decoded_fields.begin(), decoded_fields.end(),
                         [&](const auto &field) { return field.name == name; });
  if (it == decoded_fields.end()) {
    decoded_fields.push_back(MinstDecodedField{
        .name = name,
        .signed_hint = signed_hint,
        .bit_width = bit_width,
        .logical_value = logical_value,
    });
    return;
  }
  it->signed_hint = signed_hint;
  it->bit_width = bit_width;
  it->logical_value = logical_value;
}

const MinstDecodedField *Minst::FindDecodedField(std::string_view name) const noexcept {
  const auto it = std::find_if(decoded_fields.begin(), decoded_fields.end(),
                               [&](const auto &field) { return field.name == name; });
  return it == decoded_fields.end() ? nullptr : &(*it);
}

std::optional<std::int64_t> Minst::GetFieldSigned(std::string_view name) const noexcept {
  const auto *field = FindDecodedField(name);
  if (field == nullptr) {
    return std::nullopt;
  }
  return field->logical_value;
}

std::optional<std::uint64_t> Minst::GetFieldUnsigned(std::string_view name) const noexcept {
  const auto *field = FindDecodedField(name);
  if (field == nullptr) {
    return std::nullopt;
  }
  return field->UnsignedValue();
}

MinstOperand &Minst::AddSrc(MinstOperandKind kind, std::uint64_t value, std::uint16_t encoded_bits,
                            std::string_view field_name_value, std::string annotation_value) {
  srcs.push_back(MinstOperand{
      .is_dst = false,
      .ready = false,
      .kind = kind,
      .logical_width = WidthFromBits(encoded_bits),
      .encoded_bits = encoded_bits,
      .value = value,
      .data = 0,
      .field_name = field_name_value,
      .annotation = std::move(annotation_value),
  });
  return srcs.back();
}

MinstOperand &Minst::AddDst(MinstOperandKind kind, std::uint64_t value, std::uint16_t encoded_bits,
                            std::string_view field_name_value, std::string annotation_value) {
  dsts.push_back(MinstOperand{
      .is_dst = true,
      .ready = false,
      .kind = kind,
      .logical_width = WidthFromBits(encoded_bits),
      .encoded_bits = encoded_bits,
      .value = value,
      .data = 0,
      .field_name = field_name_value,
      .annotation = std::move(annotation_value),
  });
  return dsts.back();
}

MinstMemoryInfo &Minst::InitMemory(bool load, bool store, MinstWidth width_value,
                                   std::uint64_t size_value, std::string annotation_value) {
  memory = MinstMemoryInfo{
      .valid = true,
      .is_load = load,
      .is_store = store,
      .logical_width = width_value,
      .addr = 0,
      .size = size_value,
      .annotation = std::move(annotation_value),
  };
  return *memory;
}

void Minst::RebuildTypedViews() {
  srcs.clear();
  dsts.clear();
  immediates.clear();
  shift_amount.reset();
  memory.reset();
  is_branch = false;
  is_control = false;

  if (uop_big_kind == "ALU") {
    opcode_class = MinstOpcodeClass::Integer;
  } else if (uop_big_kind == "FSU") {
    opcode_class = MinstOpcodeClass::FloatingPoint;
  } else if (uop_big_kind == "BRU") {
    opcode_class = MinstOpcodeClass::Branch;
  } else if (uop_big_kind == "AMO") {
    opcode_class = MinstOpcodeClass::Atomic;
  } else if (uop_big_kind == "SYS" || uop_big_kind == "CMD") {
    opcode_class = MinstOpcodeClass::System;
  } else if (uop_big_kind == "AGU") {
    if (uop_group.starts_with("LDA")) {
      opcode_class = MinstOpcodeClass::Load;
    } else if (uop_group.starts_with("STA")) {
      opcode_class = MinstOpcodeClass::Store;
    }
  } else {
    opcode_class = MinstOpcodeClass::Invalid;
  }

  is_branch = opcode_class == MinstOpcodeClass::Branch;
  is_control = is_branch || opcode_class == MinstOpcodeClass::System || uop_big_kind == "CMD";

  for (const auto &field : decoded_fields) {
    const auto kind = KindFromField(field.name);
    if (IsSourceField(field.name)) {
      AddSrc(kind, field.UnsignedValue(), field.bit_width, field.name, "decoded source field");
    } else if (IsDestinationField(field.name)) {
      AddDst(kind, field.UnsignedValue(), field.bit_width, field.name, "decoded destination field");
    }

    if (IsImmediateField(field.name)) {
      immediates.push_back(field);
      if (field.name == "shamt") {
        shift_amount = field;
      }
    }
  }

  if (opcode_class == MinstOpcodeClass::Load || opcode_class == MinstOpcodeClass::Store ||
      opcode_class == MinstOpcodeClass::Atomic) {
    const bool is_load = opcode_class == MinstOpcodeClass::Load;
    const bool is_store = opcode_class == MinstOpcodeClass::Store;
    std::uint64_t size = 0;
    if (shift_amount.has_value()) {
      size = shift_amount->UnsignedValue();
    }
    InitMemory(is_load, is_store, MinstWidth::None, size, std::string(uop_group));
  }
}

void Minst::MarkStage(MinstStage new_stage) {
  stage = new_stage;
  if (lifecycle == MinstLifecycle::Allocated && new_stage != MinstStage::Fetch) {
    lifecycle = MinstLifecycle::InFlight;
  }
}

void Minst::MarkRetired() {
  stage = MinstStage::Retire;
  lifecycle = MinstLifecycle::Retired;
}

void Minst::MarkFlushed() {
  stage = MinstStage::Flush;
  lifecycle = MinstLifecycle::Flushed;
}

void Minst::MarkTraced() {
  stage = MinstStage::Dfx;
  lifecycle = MinstLifecycle::Traced;
}

bool Minst::IsTerminal() const noexcept {
  return lifecycle == MinstLifecycle::Retired || lifecycle == MinstLifecycle::Flushed ||
         lifecycle == MinstLifecycle::Traced;
}

std::string Minst::Assemble() const {
  if (!asm_template.empty()) {
    std::string text(asm_template);
    const auto replacements = BuildAsmReplacements(decoded_fields);
    for (const auto &replacement : replacements) {
      ReplaceAsmToken(text, replacement.needle, replacement.replacement);
    }
    return text;
  }

  std::ostringstream oss;
  oss << (mnemonic.empty() ? "invalid" : std::string(mnemonic));
  if (!decoded_fields.empty()) {
    oss << ' ';
    for (std::size_t idx = 0; idx < decoded_fields.size(); ++idx) {
      if (idx != 0) {
        oss << ", ";
      }
      oss << decoded_fields[idx].name << '=';
      if (decoded_fields[idx].signed_hint) {
        oss << decoded_fields[idx].logical_value;
      } else {
        oss << "0x" << std::hex << decoded_fields[idx].UnsignedValue() << std::dec;
      }
    }
  }
  return oss.str();
}

std::string Minst::ToString() const {
  return DumpString(*this);
}

void Minst::DumpFields(::linx::model::PacketDumpWriter &writer) const {
  writer.Field("uid", uid);
  writer.Field("pc", pc);
  writer.Field("npc", next_pc);
  writer.Field("raw", MinstEncodedWord{
                          .bits = raw_bits,
                          .length_bits = length_bits,
                          .valid = true,
                      });
  writer.Field("form", form_id.empty() ? std::string("-") : std::string(form_id));
  writer.Field("mnemonic", mnemonic.empty() ? std::string("-") : std::string(mnemonic));
  writer.Field("asm", Assemble());
  writer.Field("asm_template", asm_template.empty() ? std::string("-") : std::string(asm_template));
  writer.Field("encoding_kind",
               encoding_kind.empty() ? std::string("-") : std::string(encoding_kind));
  writer.Field("group", group.empty() ? std::string("-") : std::string(group));
  writer.Field("uop_group", uop_group.empty() ? std::string("-") : std::string(uop_group));
  writer.Field("uop_big_kind", uop_big_kind.empty() ? std::string("-") : std::string(uop_big_kind));
  writer.Field("class", ::linx::model::isa::ToString(opcode_class));
  writer.Field("stage", ::linx::model::isa::ToString(stage));
  writer.Field("life", ::linx::model::isa::ToString(lifecycle));
  writer.Field("decode", ::linx::model::isa::ToString(decode_status));
  writer.Field("encode", ::linx::model::isa::ToString(encode_status));
  writer.Field("valid_form", valid_form);
  writer.Field("thread", thread_id);
  writer.Field("lane", lane_id);
  writer.Field("rob", rob_id);
  writer.Field("valid", valid);
  writer.Field("spec", speculative);
  writer.Field("xcpt", exception);
  writer.Field("poison", poison);
  writer.Field("is_branch", is_branch);
  writer.Field("is_control", is_control);
  writer.Field("srcs", srcs.size());
  writer.Field("src_detail", JoinOperandDump(srcs));
  writer.Field("dsts", dsts.size());
  writer.Field("dst_detail", JoinOperandDump(dsts));
  writer.Field("imms", immediates.size());
  writer.Field("imm_detail", JoinFieldDump(immediates));
  writer.Field("shift", shift_amount.has_value() ? DumpString(*shift_amount) : std::string("null"));
  writer.Field("mem", memory.has_value() ? DumpString(*memory) : std::string("null"));
  writer.Field("fields", JoinFieldDump(decoded_fields));
}

} // namespace linx::model::isa
