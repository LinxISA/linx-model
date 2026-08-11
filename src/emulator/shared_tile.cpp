#include "linx/model/emulator/state.hpp"

#include <algorithm>
#include <bit>

namespace linx::model::emulator {

namespace {

constexpr std::uint8_t kValidPeMask = (1U << kLinxCorePeCount) - 1U;

bool DescriptorValid(const SharedTileDescriptor &descriptor, std::uint32_t capacity) noexcept {
  if (descriptor.rows == 0 || descriptor.cols == 0 || descriptor.valid_rows > descriptor.rows ||
      descriptor.valid_cols > descriptor.cols) {
    return false;
  }
  const auto elements = static_cast<std::uint64_t>(descriptor.rows) * descriptor.cols;
  return elements != 0 && elements <= capacity * 2ULL;
}

} // namespace

std::optional<std::uint32_t> SharedTileBank::CapacityBytes(std::uint8_t size_code) noexcept {
  if (size_code < 1U || size_code > 7U) {
    return std::nullopt;
  }
  return 128U << (size_code - 1U);
}

SharedTileWriteStatus
SharedTileBank::Write(std::uint16_t shared_id, std::uint8_t pe_mask, std::uint8_t size_code,
                      const SharedTileDescriptor &descriptor,
                      const std::array<std::vector<std::uint8_t>, kLinxCorePeCount> &payloads) {
  std::array<SharedTileDescriptor, kLinxCorePeCount> descriptors{};
  descriptors.fill(descriptor);
  return Write(shared_id, pe_mask, size_code, descriptors, payloads);
}

SharedTileWriteStatus
SharedTileBank::Write(std::uint16_t shared_id, std::uint8_t pe_mask, std::uint8_t size_code,
                      const std::array<SharedTileDescriptor, kLinxCorePeCount> &descriptors,
                      const std::array<std::vector<std::uint8_t>, kLinxCorePeCount> &payloads) {
  if (shared_id >= versions_.size()) {
    return SharedTileWriteStatus::InvalidRegister;
  }
  if ((pe_mask & ~kValidPeMask) != 0U) {
    return SharedTileWriteStatus::InvalidMask;
  }
  if (pe_mask == 0U) {
    return SharedTileWriteStatus::Noop;
  }
  const auto capacity = CapacityBytes(size_code);
  if (!capacity.has_value()) {
    return SharedTileWriteStatus::InvalidSize;
  }

  auto &version = versions_[shared_id];
  if (version.allocation_mask != 0U && (pe_mask & ~version.allocation_mask) != 0U) {
    return SharedTileWriteStatus::AllocationExpansion;
  }

  for (std::size_t pe = 0; pe < kLinxCorePeCount; ++pe) {
    const auto bit = static_cast<std::uint8_t>(1U << pe);
    if ((pe_mask & bit) == 0U) {
      continue;
    }
    if (!DescriptorValid(descriptors[pe], *capacity)) {
      return SharedTileWriteStatus::InvalidDescriptor;
    }
    if (payloads[pe].size() != *capacity) {
      return SharedTileWriteStatus::PayloadSizeMismatch;
    }
    if (version.allocation_mask != 0U &&
        (version.per_pe_capacity != *capacity || version.dtype != descriptors[pe].dtype)) {
      return SharedTileWriteStatus::DescriptorMismatch;
    }
    if ((version.initialized_mask & bit) != 0U && version.lanes[pe].descriptor != descriptors[pe]) {
      return SharedTileWriteStatus::DescriptorMismatch;
    }
  }

  if (version.allocation_mask == 0U) {
    version.allocation_mask = pe_mask;
    version.per_pe_capacity = *capacity;
    version.allocated_bytes = *capacity * static_cast<std::uint32_t>(std::popcount(pe_mask));
    for (std::size_t pe = 0; pe < kLinxCorePeCount; ++pe) {
      if ((pe_mask & static_cast<std::uint8_t>(1U << pe)) != 0U) {
        version.dtype = descriptors[pe].dtype;
        break;
      }
    }
  }

  for (std::size_t pe = 0; pe < kLinxCorePeCount; ++pe) {
    const auto bit = static_cast<std::uint8_t>(1U << pe);
    if ((pe_mask & bit) == 0U) {
      continue;
    }
    version.lanes[pe].descriptor = descriptors[pe];
    version.lanes[pe].data = payloads[pe];
    version.initialized_mask |= bit;
  }
  return SharedTileWriteStatus::Applied;
}

const SharedTileLane *SharedTileBank::Read(std::uint16_t shared_id,
                                           std::uint8_t pe_id) const noexcept {
  if (shared_id >= versions_.size() || pe_id >= kLinxCorePeCount) {
    return nullptr;
  }
  const auto &version = versions_[shared_id];
  if ((version.initialized_mask & static_cast<std::uint8_t>(1U << pe_id)) == 0U) {
    return nullptr;
  }
  return &version.lanes[pe_id];
}

const SharedTileVersion &SharedTileBank::Version(std::uint16_t shared_id) const noexcept {
  static const SharedTileVersion kInvalid{};
  return shared_id < versions_.size() ? versions_[shared_id] : kInvalid;
}

void SharedTileBank::Reset() noexcept {
  versions_ = {};
}

bool BindingAllows(TileBindingKind binding, TileOperandSpace space) noexcept {
  switch (binding) {
  case TileBindingKind::Bior:
    return space == TileOperandSpace::ScalarAddress;
  case TileBindingKind::Biot:
    return space == TileOperandSpace::Local;
  case TileBindingKind::Bios:
    return space == TileOperandSpace::Shared;
  }
  return false;
}

bool ValidateSharedOperation(TileOperationKind operation, TileSharedUse shared_use,
                             std::uint8_t shared_mask, std::uint8_t local_mask) noexcept {
  if ((shared_mask & ~kValidPeMask) != 0U || (local_mask & ~kValidPeMask) != 0U) {
    return false;
  }
  if (shared_mask == 0U) {
    return true;
  }
  switch (operation) {
  case TileOperationKind::Tmov:
    return shared_use != TileSharedUse::None && shared_mask == local_mask;
  case TileOperationKind::Cube:
    return shared_use == TileSharedUse::Source && shared_mask == kValidPeMask;
  case TileOperationKind::Tgemv:
    return shared_use == TileSharedUse::None;
  }
  return false;
}

} // namespace linx::model::emulator
