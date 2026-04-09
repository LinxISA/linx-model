#include "linx/model/elf_loader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace linx::model {

namespace {

constexpr std::array<std::uint8_t, 4> kElfMagic = {0x7f, 'E', 'L', 'F'};
constexpr std::uint8_t kElfClass32 = 1;
constexpr std::uint8_t kElfClass64 = 2;
constexpr std::uint8_t kElfDataLittle = 1;
constexpr std::uint16_t kElfTypeRel = 1;
constexpr std::uint16_t kElfTypeExec = 2;
constexpr std::uint16_t kElfTypeDyn = 3;
constexpr std::uint32_t kPtLoad = 1;
constexpr std::uint32_t kPfX = 1;
constexpr std::uint64_t kShfAlloc = 0x2;
constexpr std::uint64_t kShfExecInstr = 0x4;
constexpr std::uint32_t kShtNoBits = 8;

[[nodiscard]] std::uint16_t ReadLe16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 2 > bytes.size()) {
    throw std::runtime_error("ELF parse: truncated u16");
  }
  return static_cast<std::uint16_t>(bytes[offset]) |
         (static_cast<std::uint16_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t ReadLe32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) {
    throw std::runtime_error("ELF parse: truncated u32");
  }
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::uint64_t ReadLe64(std::span<const std::uint8_t> bytes, std::size_t offset) {
  if (offset + 8 > bytes.size()) {
    throw std::runtime_error("ELF parse: truncated u64");
  }
  std::uint64_t value = 0;
  for (std::size_t idx = 0; idx < 8; ++idx) {
    value |= static_cast<std::uint64_t>(bytes[offset + idx]) << (idx * 8U);
  }
  return value;
}

[[nodiscard]] std::string SegmentName(std::size_t index, bool executable) {
  return executable ? ".text[" + std::to_string(index) + "]"
                    : ".load[" + std::to_string(index) + "]";
}

[[nodiscard]] std::string SectionName(std::size_t index, bool executable) {
  return executable ? ".text.rel[" + std::to_string(index) + "]"
                    : ".alloc.rel[" + std::to_string(index) + "]";
}

template <class T> [[nodiscard]] T NarrowCast(std::uint64_t value, const char *context) {
  if (value > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
    throw std::runtime_error(std::string("ELF parse: overflow for ") + context);
  }
  return static_cast<T>(value);
}

} // namespace

bool IsElfImage(std::span<const std::uint8_t> bytes) noexcept {
  return bytes.size() >= kElfMagic.size() &&
         std::equal(kElfMagic.begin(), kElfMagic.end(), bytes.begin());
}

std::vector<std::uint8_t> ReadBinaryFile(std::string_view path) {
  std::ifstream input(std::string(path), std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open file: " + std::string(path));
  }

  input.seekg(0, std::ios::end);
  const auto end_pos = input.tellg();
  if (end_pos < 0) {
    throw std::runtime_error("failed to determine file size: " + std::string(path));
  }
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end_pos));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
      throw std::runtime_error("failed to read file: " + std::string(path));
    }
  }
  return bytes;
}

ProgramImage LoadRawBinaryImageFromBytes(std::span<const std::uint8_t> bytes,
                                         std::string_view source_path, std::uint64_t base_address,
                                         std::string_view section_name) {
  ProgramImage image;
  image.source_path = std::string(source_path);
  image.entry_point = base_address;
  image.sections.push_back(ProgramSection{
      .name = std::string(section_name),
      .address = base_address,
      .bytes = std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
      .executable = true,
  });
  return image;
}

ProgramImage LoadRawBinaryImageFromFile(std::string_view path, std::uint64_t base_address,
                                        std::string_view section_name) {
  const auto bytes = ReadBinaryFile(path);
  return LoadRawBinaryImageFromBytes(bytes, path, base_address, section_name);
}

ProgramImage LoadElfImageFromBytes(std::span<const std::uint8_t> bytes,
                                   std::string_view source_path) {
  if (!IsElfImage(bytes)) {
    throw std::runtime_error("not an ELF image: " + std::string(source_path));
  }
  if (bytes.size() < 16) {
    throw std::runtime_error("ELF parse: truncated e_ident");
  }
  if (bytes[4] != kElfClass32 && bytes[4] != kElfClass64) {
    throw std::runtime_error("ELF parse: unsupported class");
  }
  if (bytes[5] != kElfDataLittle) {
    throw std::runtime_error("ELF parse: only little-endian ELF is supported");
  }

  const bool is_64 = bytes[4] == kElfClass64;
  const auto type = ReadLe16(bytes, 16);
  if (type != kElfTypeRel && type != kElfTypeExec && type != kElfTypeDyn) {
    throw std::runtime_error("ELF parse: unsupported ELF type");
  }

  ProgramImage image;
  image.source_path = std::string(source_path);

  std::uint64_t phoff = 0;
  std::uint16_t phentsize = 0;
  std::uint16_t phnum = 0;
  std::uint64_t shoff = 0;
  std::uint16_t shentsize = 0;
  std::uint16_t shnum = 0;

  if (is_64) {
    if (bytes.size() < 64) {
      throw std::runtime_error("ELF parse: truncated ELF64 header");
    }
    image.entry_point = ReadLe64(bytes, 24);
    phoff = ReadLe64(bytes, 32);
    shoff = ReadLe64(bytes, 40);
    phentsize = ReadLe16(bytes, 54);
    phnum = ReadLe16(bytes, 56);
    shentsize = ReadLe16(bytes, 58);
    shnum = ReadLe16(bytes, 60);
  } else {
    if (bytes.size() < 52) {
      throw std::runtime_error("ELF parse: truncated ELF32 header");
    }
    image.entry_point = ReadLe32(bytes, 24);
    phoff = ReadLe32(bytes, 28);
    shoff = ReadLe32(bytes, 32);
    phentsize = ReadLe16(bytes, 42);
    phnum = ReadLe16(bytes, 44);
    shentsize = ReadLe16(bytes, 46);
    shnum = ReadLe16(bytes, 48);
  }

  if (type == kElfTypeRel) {
    std::uint64_t next_address = 0;
    if (shentsize == 0 || shnum == 0) {
      throw std::runtime_error("ELF parse: relocatable image has no section headers");
    }

    for (std::uint16_t index = 0; index < shnum; ++index) {
      const std::size_t offset = NarrowCast<std::size_t>(
          shoff + static_cast<std::uint64_t>(index) * shentsize, "section header offset");
      if (offset + shentsize > bytes.size()) {
        throw std::runtime_error("ELF parse: truncated section header table");
      }

      std::uint32_t type_value = 0;
      std::uint64_t flags = 0;
      std::uint64_t address = 0;
      std::uint64_t file_offset = 0;
      std::uint64_t file_size = 0;

      if (is_64) {
        type_value = ReadLe32(bytes, offset + 4);
        flags = ReadLe64(bytes, offset + 8);
        address = ReadLe64(bytes, offset + 16);
        file_offset = ReadLe64(bytes, offset + 24);
        file_size = ReadLe64(bytes, offset + 32);
      } else {
        type_value = ReadLe32(bytes, offset + 4);
        flags = ReadLe32(bytes, offset + 8);
        address = ReadLe32(bytes, offset + 12);
        file_offset = ReadLe32(bytes, offset + 16);
        file_size = ReadLe32(bytes, offset + 20);
      }

      if ((flags & kShfAlloc) == 0U || file_size == 0U) {
        continue;
      }

      if (address == 0U) {
        address = next_address;
      }
      next_address = std::max(next_address, address + file_size + 0x10U);

      const bool executable = (flags & kShfExecInstr) != 0U;
      std::vector<std::uint8_t> payload;
      if (type_value == kShtNoBits) {
        payload.resize(NarrowCast<std::size_t>(file_size, "section size"), 0);
      } else {
        const std::size_t begin = NarrowCast<std::size_t>(file_offset, "section offset");
        const std::size_t size = NarrowCast<std::size_t>(file_size, "section size");
        if (begin + size > bytes.size()) {
          throw std::runtime_error("ELF parse: relocatable section exceeds file size");
        }
        payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                       bytes.begin() + static_cast<std::ptrdiff_t>(begin + size));
      }

      image.sections.push_back(ProgramSection{
          .name = SectionName(index, executable),
          .address = address,
          .bytes = std::move(payload),
          .executable = executable,
      });
    }

    if (image.sections.empty()) {
      throw std::runtime_error("ELF parse: relocatable image has no alloc sections");
    }
    image.entry_point = image.sections.front().address;
    return image;
  }

  if (phentsize == 0 || phnum == 0) {
    throw std::runtime_error("ELF parse: image has no program headers");
  }

  for (std::uint16_t index = 0; index < phnum; ++index) {
    const std::size_t offset = NarrowCast<std::size_t>(
        phoff + static_cast<std::uint64_t>(index) * phentsize, "program header offset");
    if (offset + phentsize > bytes.size()) {
      throw std::runtime_error("ELF parse: truncated program header table");
    }

    std::uint32_t type_value = 0;
    std::uint32_t flags = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t virtual_address = 0;
    std::uint64_t file_size = 0;

    if (is_64) {
      type_value = ReadLe32(bytes, offset + 0);
      flags = ReadLe32(bytes, offset + 4);
      file_offset = ReadLe64(bytes, offset + 8);
      virtual_address = ReadLe64(bytes, offset + 16);
      file_size = ReadLe64(bytes, offset + 32);
    } else {
      type_value = ReadLe32(bytes, offset + 0);
      file_offset = ReadLe32(bytes, offset + 4);
      virtual_address = ReadLe32(bytes, offset + 8);
      file_size = ReadLe32(bytes, offset + 16);
      flags = ReadLe32(bytes, offset + 24);
    }

    if (type_value != kPtLoad || file_size == 0) {
      continue;
    }

    const std::size_t begin = NarrowCast<std::size_t>(file_offset, "segment offset");
    const std::size_t size = NarrowCast<std::size_t>(file_size, "segment size");
    if (begin + size > bytes.size()) {
      throw std::runtime_error("ELF parse: segment exceeds file size");
    }

    const bool executable = (flags & kPfX) != 0U;
    image.sections.push_back(ProgramSection{
        .name = SegmentName(index, executable),
        .address = virtual_address,
        .bytes =
            std::vector<std::uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(begin),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(begin + size)),
        .executable = executable,
    });
  }

  if (image.sections.empty()) {
    throw std::runtime_error("ELF parse: no PT_LOAD segments found");
  }

  return image;
}

ProgramImage LoadElfImageFromFile(std::string_view path) {
  const auto bytes = ReadBinaryFile(path);
  return LoadElfImageFromBytes(bytes, path);
}

ProgramImage LoadProgramImageFromFile(std::string_view path, std::uint64_t raw_base_address) {
  const auto bytes = ReadBinaryFile(path);
  if (IsElfImage(bytes)) {
    return LoadElfImageFromBytes(bytes, path);
  }
  return LoadRawBinaryImageFromBytes(bytes, path, raw_base_address);
}

} // namespace linx::model
