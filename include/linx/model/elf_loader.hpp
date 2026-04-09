#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "linx/model/program_image.hpp"

namespace linx::model {

[[nodiscard]] bool IsElfImage(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] std::vector<std::uint8_t> ReadBinaryFile(std::string_view path);

[[nodiscard]] ProgramImage LoadElfImageFromBytes(std::span<const std::uint8_t> bytes,
                                                 std::string_view source_path);
[[nodiscard]] ProgramImage LoadElfImageFromFile(std::string_view path);

[[nodiscard]] ProgramImage LoadRawBinaryImageFromBytes(std::span<const std::uint8_t> bytes,
                                                       std::string_view source_path,
                                                       std::uint64_t base_address = 0,
                                                       std::string_view section_name = ".text");
[[nodiscard]] ProgramImage LoadRawBinaryImageFromFile(std::string_view path,
                                                      std::uint64_t base_address = 0,
                                                      std::string_view section_name = ".text");

[[nodiscard]] ProgramImage LoadProgramImageFromFile(std::string_view path,
                                                    std::uint64_t raw_base_address = 0);

} // namespace linx::model
