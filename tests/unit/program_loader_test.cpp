#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "linx/model.hpp"

namespace {

namespace fs = std::filesystem;

using linx::model::LoadProgramImageFromFile;
using linx::model::ProgramImage;
using linx::model::isa::DisassembleProgram;
using linx::model::isa::EncodeMinst;
using linx::model::isa::LookupFormByMnemonic;
using linx::model::isa::Minst;
using linx::model::isa::MinstCodecStatus;

std::vector<std::uint8_t> EncodeAddBytes() {
  const auto *form = LookupFormByMnemonic("ADD");
  if (form == nullptr) {
    return {};
  }

  Minst inst;
  inst.SetForm(form);
  for (const auto &field : linx::model::isa::FieldsFor(*form)) {
    inst.SetDecodedField(field.name, 0, field.signed_hint > 0, field.bit_width);
  }
  inst.RebuildTypedViews();

  const auto encoded = EncodeMinst(inst);
  if (!encoded.valid || encoded.status != MinstCodecStatus::Ok) {
    return {};
  }

  const auto size_bytes = static_cast<std::size_t>(encoded.length_bits / 8);
  std::vector<std::uint8_t> bytes(size_bytes);
  for (std::size_t idx = 0; idx < size_bytes; ++idx) {
    bytes[idx] = static_cast<std::uint8_t>((encoded.bits >> (idx * 8U)) & 0xffU);
  }
  return bytes;
}

void WriteFile(const fs::path &path, const std::vector<std::uint8_t> &bytes) {
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> BuildElf64Image(const std::vector<std::uint8_t> &text,
                                          std::uint64_t vaddr) {
  constexpr std::size_t kHeaderSize = 64;
  constexpr std::size_t kPhdrSize = 56;
  constexpr std::size_t kTextOffset = 0x100;

  std::vector<std::uint8_t> bytes(kTextOffset + text.size(), 0);
  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 2;
  bytes[5] = 1;
  bytes[6] = 1;

  auto write16 = [&](std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value & 0xffU);
    bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
  };
  auto write32 = [&](std::size_t offset, std::uint32_t value) {
    for (std::size_t idx = 0; idx < 4; ++idx) {
      bytes[offset + idx] = static_cast<std::uint8_t>((value >> (idx * 8U)) & 0xffU);
    }
  };
  auto write64 = [&](std::size_t offset, std::uint64_t value) {
    for (std::size_t idx = 0; idx < 8; ++idx) {
      bytes[offset + idx] = static_cast<std::uint8_t>((value >> (idx * 8U)) & 0xffU);
    }
  };

  write16(16, 2);
  write16(18, 0xf00d);
  write32(20, 1);
  write64(24, vaddr);
  write64(32, kHeaderSize);
  write64(40, 0);
  write32(48, 0);
  write16(52, kHeaderSize);
  write16(54, kPhdrSize);
  write16(56, 1);
  write16(58, 0);
  write16(60, 0);
  write16(62, 0);

  write32(kHeaderSize + 0, 1);
  write32(kHeaderSize + 4, 5);
  write64(kHeaderSize + 8, kTextOffset);
  write64(kHeaderSize + 16, vaddr);
  write64(kHeaderSize + 24, vaddr);
  write64(kHeaderSize + 32, text.size());
  write64(kHeaderSize + 40, text.size());
  write64(kHeaderSize + 48, 0x1000);

  std::copy(text.begin(), text.end(), bytes.begin() + static_cast<std::ptrdiff_t>(kTextOffset));
  return bytes;
}

int RunRawBinarySmoke() {
  const auto bytes = EncodeAddBytes();
  if (bytes.empty()) {
    return 1;
  }

  const ProgramImage image = linx::model::LoadRawBinaryImageFromBytes(bytes, "raw-add.bin", 0x2000);
  if (image.entry_point != 0x2000 || image.ExecutableSectionCount() != 1) {
    return 2;
  }

  const auto lines = DisassembleProgram(image);
  if (lines.size() != 1 || lines.front().pc != 0x2000) {
    return 3;
  }
  if (lines.front().text.find("add") == std::string::npos) {
    return 4;
  }

  return 0;
}

int RunElfFileSmoke() {
  const auto text = EncodeAddBytes();
  if (text.empty()) {
    return 10;
  }

  const auto tmp_path = fs::temp_directory_path() / "linx_model_program_loader_test.elf";
  const auto elf = BuildElf64Image(text, 0x1000);
  WriteFile(tmp_path, elf);

  const auto cleanup = [&]() {
    std::error_code ec;
    fs::remove(tmp_path, ec);
  };
  cleanup();
  WriteFile(tmp_path, elf);

  try {
    const ProgramImage image = LoadProgramImageFromFile(tmp_path.string());
    if (image.entry_point != 0x1000 || image.ExecutableSectionCount() != 1) {
      cleanup();
      return 11;
    }
    const auto lines = DisassembleProgram(image);
    if (lines.size() != 1 || lines.front().pc != 0x1000) {
      cleanup();
      return 12;
    }
    if (lines.front().text.find("add") == std::string::npos) {
      cleanup();
      return 13;
    }
  } catch (...) {
    cleanup();
    return 14;
  }

  cleanup();
  return 0;
}

} // namespace

int main() {
  if (RunRawBinarySmoke() != 0) {
    return 1;
  }
  if (RunElfFileSmoke() != 0) {
    return 2;
  }
  return 0;
}
