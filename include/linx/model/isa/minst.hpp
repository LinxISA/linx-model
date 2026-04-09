#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "linx/model/packet_dump.hpp"

namespace linx::model::isa {

struct MinstFormDesc;

enum class MinstOperandKind : std::uint8_t {
  Invalid = 0,
  Register,
  Immediate,
  Predicate,
  Memory,
  Control,
  Metadata,
};

enum class MinstWidth : std::uint8_t {
  None = 0,
  B8,
  B16,
  B32,
  B64,
  B128,
};

enum class MinstOpcodeClass : std::uint8_t {
  Invalid = 0,
  Nop,
  Integer,
  FloatingPoint,
  Branch,
  Load,
  Store,
  Atomic,
  System,
};

enum class MinstStage : std::uint8_t {
  Invalid = 0,
  Fetch,
  Decode,
  Rename,
  Dispatch,
  Issue,
  Execute,
  Memory,
  Writeback,
  Rob,
  Retire,
  Flush,
  Dfx,
};

enum class MinstLifecycle : std::uint8_t {
  Invalid = 0,
  Allocated,
  InFlight,
  Retired,
  Flushed,
  Traced,
};

enum class MinstCodecStatus : std::uint8_t {
  Ok = 0,
  InvalidLength,
  NoMatch,
  AmbiguousMatch,
  ConstraintViolation,
  InvalidForm,
  MissingField,
  ValueOutOfRange,
};

[[nodiscard]] std::string_view ToString(MinstOperandKind kind) noexcept;
[[nodiscard]] std::string_view ToString(MinstWidth width) noexcept;
[[nodiscard]] std::string_view ToString(MinstOpcodeClass opcode_class) noexcept;
[[nodiscard]] std::string_view ToString(MinstStage stage) noexcept;
[[nodiscard]] std::string_view ToString(MinstLifecycle lifecycle) noexcept;
[[nodiscard]] std::string_view ToString(MinstCodecStatus status) noexcept;

struct MinstDecodedField {
  std::string_view name;
  bool signed_hint = false;
  std::uint16_t bit_width = 0;
  std::int64_t logical_value = 0;

  [[nodiscard]] std::uint64_t UnsignedValue() const noexcept;
  void DumpFields(::linx::model::PacketDumpWriter &writer) const;
};

struct MinstOperand {
  bool is_dst = false;
  bool ready = false;
  MinstOperandKind kind = MinstOperandKind::Invalid;
  MinstWidth logical_width = MinstWidth::None;
  std::uint16_t encoded_bits = 0;
  std::uint64_t value = 0;
  std::uint64_t data = 0;
  std::string_view field_name;
  std::string annotation;

  void DumpFields(::linx::model::PacketDumpWriter &writer) const;
};

struct MinstMemoryInfo {
  bool valid = false;
  bool is_load = false;
  bool is_store = false;
  MinstWidth logical_width = MinstWidth::None;
  std::uint64_t addr = 0;
  std::uint64_t size = 0;
  std::string annotation;

  void DumpFields(::linx::model::PacketDumpWriter &writer) const;
};

struct MinstEncodedWord {
  std::uint64_t bits = 0;
  std::uint8_t length_bits = 0;
  bool valid = false;
  MinstCodecStatus status = MinstCodecStatus::InvalidForm;

  [[nodiscard]] std::string HexString() const;
  void DumpFields(::linx::model::PacketDumpWriter &writer) const;
};

struct Minst {
  using Ptr = std::unique_ptr<Minst>;
  using SharedPtr = std::shared_ptr<Minst>;

  Minst();

  std::uint64_t uid = 0;
  std::uint64_t fetch_seq = 0;
  std::uint64_t pc = 0;
  std::uint64_t next_pc = 0;
  std::uint32_t thread_id = 0;
  std::uint32_t lane_id = 0;
  std::uint32_t rob_id = 0;

  bool valid = true;
  bool speculative = false;
  bool exception = false;
  bool poison = false;

  std::uint64_t raw_bits = 0;
  std::uint8_t length_bits = 0;
  std::uint8_t code_bytes = 0;

  MinstOpcodeClass opcode_class = MinstOpcodeClass::Invalid;
  MinstStage stage = MinstStage::Fetch;
  MinstLifecycle lifecycle = MinstLifecycle::Allocated;
  MinstCodecStatus decode_status = MinstCodecStatus::NoMatch;
  MinstCodecStatus encode_status = MinstCodecStatus::InvalidForm;
  bool valid_form = false;
  bool is_branch = false;
  bool is_control = false;

  const MinstFormDesc *form = nullptr;
  std::string_view form_id;
  std::string_view mnemonic;
  std::string_view asm_template;
  std::string_view encoding_kind;
  std::string_view group;
  std::string_view uop_group;
  std::string_view uop_big_kind;
  std::string annotation;

  std::vector<MinstDecodedField> decoded_fields;
  std::vector<MinstOperand> srcs;
  std::vector<MinstOperand> dsts;
  std::vector<MinstDecodedField> immediates;
  std::optional<MinstDecodedField> shift_amount;
  std::optional<MinstMemoryInfo> memory;

  static Ptr CreateFetch(std::uint64_t uid_value, std::uint64_t pc_value,
                         std::uint64_t raw_value = 0, std::uint8_t code_size = 4);
  static SharedPtr Share(Ptr packet);

  void ClearDecodedState();
  void SetForm(const MinstFormDesc *desc);
  void SetRawEncoding(std::uint64_t bits_value, std::uint8_t length_bits_value);
  void SetDecodedField(std::string_view name, std::int64_t logical_value, bool signed_hint,
                       std::uint16_t bit_width);

  [[nodiscard]] const MinstDecodedField *FindDecodedField(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<std::int64_t> GetFieldSigned(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<std::uint64_t> GetFieldUnsigned(std::string_view name) const noexcept;

  MinstOperand &AddSrc(MinstOperandKind kind, std::uint64_t value, std::uint16_t encoded_bits,
                       std::string_view field_name, std::string annotation = {});
  MinstOperand &AddDst(MinstOperandKind kind, std::uint64_t value, std::uint16_t encoded_bits,
                       std::string_view field_name, std::string annotation = {});
  MinstMemoryInfo &InitMemory(bool load, bool store, MinstWidth width_value,
                              std::uint64_t size_value, std::string annotation = {});

  void RebuildTypedViews();
  void MarkStage(MinstStage new_stage);
  void MarkRetired();
  void MarkFlushed();
  void MarkTraced();

  [[nodiscard]] bool IsTerminal() const noexcept;
  [[nodiscard]] std::string Assemble() const;
  [[nodiscard]] std::string ToString() const;
  void DumpFields(::linx::model::PacketDumpWriter &writer) const;
};

using MinstPtr = Minst::Ptr;
using MinstSharedPtr = Minst::SharedPtr;

} // namespace linx::model::isa
