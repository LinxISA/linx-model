#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace linx::model::emulator {

constexpr std::size_t kLinxGprCount = 24;
constexpr std::size_t kLinxQueueCount = 4;
constexpr std::size_t kLinxVecQueueDepth = 32;
constexpr std::size_t kLinxAcrCount = 16;
constexpr std::size_t kLinxSsrCount = 0x1000;
constexpr std::size_t kLinxTileMaxIor = 16;
constexpr std::size_t kLinxTileMaxIot = 32;
constexpr std::size_t kLinxVecRiMax = kLinxTileMaxIor * 3;
constexpr std::size_t kLinxTileMaxBytes = 64U * 1024U;
constexpr std::size_t kLinxTileMaxWords = kLinxTileMaxBytes / 4U;
constexpr std::size_t kLinxSharedTileCount = 256;
constexpr std::size_t kLinxCorePeCount = 4;
constexpr std::size_t kLinxSharedTileMaxBytes = 8U * 1024U;

struct SharedTileDescriptor {
  std::uint32_t dtype = 0;
  std::uint16_t valid_cols = 0;
  std::uint16_t valid_rows = 0;
  std::uint16_t cols = 0;
  std::uint16_t rows = 0;

  bool operator==(const SharedTileDescriptor &) const = default;
};

struct SharedTileLane {
  SharedTileDescriptor descriptor{};
  std::vector<std::uint8_t> data{};
};

struct SharedTileVersion {
  std::array<SharedTileLane, kLinxCorePeCount> lanes{};
  std::uint32_t per_pe_capacity = 0;
  std::uint32_t allocated_bytes = 0;
  std::uint32_t dtype = 0;
  std::uint8_t allocation_mask = 0;
  std::uint8_t initialized_mask = 0;
};

enum class SharedTileWriteStatus : std::uint8_t {
  Applied,
  Noop,
  InvalidRegister,
  InvalidMask,
  InvalidSize,
  InvalidDescriptor,
  PayloadSizeMismatch,
  AllocationExpansion,
  DescriptorMismatch,
};

class SharedTileBank {
public:
  static std::optional<std::uint32_t> CapacityBytes(std::uint8_t size_code) noexcept;

  [[nodiscard]] SharedTileWriteStatus
  Write(std::uint16_t shared_id, std::uint8_t pe_mask, std::uint8_t size_code,
        const SharedTileDescriptor &descriptor,
        const std::array<std::vector<std::uint8_t>, kLinxCorePeCount> &payloads);
  [[nodiscard]] SharedTileWriteStatus
  Write(std::uint16_t shared_id, std::uint8_t pe_mask, std::uint8_t size_code,
        const std::array<SharedTileDescriptor, kLinxCorePeCount> &descriptors,
        const std::array<std::vector<std::uint8_t>, kLinxCorePeCount> &payloads);

  [[nodiscard]] const SharedTileLane *Read(std::uint16_t shared_id,
                                           std::uint8_t pe_id) const noexcept;
  [[nodiscard]] const SharedTileVersion &Version(std::uint16_t shared_id) const noexcept;
  void Reset() noexcept;

private:
  std::array<SharedTileVersion, kLinxSharedTileCount> versions_{};
};

enum class TileBindingKind : std::uint8_t { Bior, Biot, Bios };
enum class TileOperandSpace : std::uint8_t { ScalarAddress, Local, Shared };
enum class TileOperationKind : std::uint8_t { Tmov, Cube, Tgemv };
enum class TileSharedUse : std::uint8_t { None, Source, Destination };

[[nodiscard]] bool BindingAllows(TileBindingKind binding, TileOperandSpace space) noexcept;
[[nodiscard]] bool ValidateSharedOperation(TileOperationKind operation, TileSharedUse shared_use,
                                           std::uint8_t shared_mask,
                                           std::uint8_t local_mask) noexcept;

struct LinxAcrBlockState {
  std::array<std::uint64_t, kLinxQueueCount> tq{};
  std::array<std::uint64_t, kLinxQueueCount> uq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vtq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vuq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vmq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vnq{};
  std::uint64_t vec_p = 0;
  std::uint64_t bpc = 0;
  std::uint64_t tgt = 0;
  std::uint32_t cond = 0;
  std::uint32_t carg = 0;
  std::uint32_t brtype = 0;
  std::uint32_t blocktype = 0;
  std::uint32_t call_ra_set = 0;
  std::uint32_t call_setret_pending = 0;
  std::uint64_t body_tpc = 0;
  std::uint64_t body_end = 0;
  std::uint64_t return_pc = 0;
  std::uint32_t in_body = 0;
  std::uint64_t tmpl_pc = 0;
  std::uint32_t tmpl_kind = 0;
  std::uint32_t tmpl_step = 0;
  std::uint32_t tmpl_reg_cur = 0;
  std::uint32_t tmpl_reg_begin = 0;
  std::uint32_t tmpl_reg_end = 0;
  std::uint64_t tmpl_stacksize = 0;
  std::uint64_t tmpl_mem_dst = 0;
  std::uint64_t tmpl_mem_src = 0;
  std::uint64_t tmpl_mem_remaining = 0;
  std::uint64_t tmpl_mem_value = 0;
  std::array<std::uint64_t, 3> lb{};
  std::array<std::uint64_t, 3> lc{};
  std::uint32_t tile_func = 0;
  std::uint32_t tile_dtype = 0;
  std::uint32_t tile_iot_valid = 0;
  std::uint32_t tile_iot_flags = 0;
  std::uint32_t tile_iot_dst = 0;
  std::uint32_t tile_iot_grp = 0;
  std::uint32_t tile_iot_src0 = 0;
  std::uint32_t tile_iot_src1 = 0;
  std::uint32_t tile_iot_reg = 0;
  std::uint32_t tile_iot_size = 0;
  std::uint32_t tile_arg_format = 0;
  std::uint32_t tile_attr_raw = 0;
  std::uint32_t tile_attr_pad = 0;
  std::uint32_t tile_attr_dtype = 0;
  std::uint32_t tile_ior_count = 0;
  std::array<std::uint64_t, kLinxTileMaxIor> tile_ior_desc{};
  std::uint32_t vec_ri_count = 0;
  std::array<std::uint64_t, kLinxVecRiMax> vec_ri_value{};
  std::uint32_t tile_iot_count = 0;
  std::array<std::uint64_t, kLinxTileMaxIot> tile_iot_desc{};
};

struct LinxState {
  std::array<std::uint64_t, kLinxGprCount> gpr{};
  std::array<std::uint64_t, kLinxQueueCount> tq{};
  std::array<std::uint64_t, kLinxQueueCount> uq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vtq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vuq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vmq{};
  std::array<std::uint64_t, kLinxVecQueueDepth> vnq{};
  std::uint64_t vec_p = 0;
  std::array<std::uint64_t, kLinxSsrCount> ssr{};
  std::array<std::array<std::uint64_t, kLinxSsrCount>, kLinxAcrCount> ssr_acr{};
  std::uint32_t acr = 0;
  std::uint32_t fcsr = 0;
  std::uint64_t tgt = 0;
  std::uint32_t cond = 0;
  std::uint32_t carg = 0;
  std::uint32_t brtype = 0;
  std::uint32_t blocktype = 0;
  std::uint32_t call_ra_set = 0;
  std::uint32_t call_setret_pending = 0;
  std::uint64_t body_tpc = 0;
  std::uint64_t body_end = 0;
  std::uint64_t return_pc = 0;
  std::uint32_t in_body = 0;
  std::uint64_t tmpl_pc = 0;
  std::uint32_t tmpl_kind = 0;
  std::uint32_t tmpl_step = 0;
  std::uint32_t tmpl_reg_cur = 0;
  std::uint32_t tmpl_reg_begin = 0;
  std::uint32_t tmpl_reg_end = 0;
  std::uint64_t tmpl_stacksize = 0;
  std::uint64_t tmpl_mem_dst = 0;
  std::uint64_t tmpl_mem_src = 0;
  std::uint64_t tmpl_mem_remaining = 0;
  std::uint64_t tmpl_mem_value = 0;
  std::array<std::uint64_t, 3> lb{};
  std::array<std::uint64_t, 3> lc{};
  std::array<LinxAcrBlockState, kLinxAcrCount> acr_block_state{};
  std::uint32_t tile_func = 0;
  std::uint32_t tile_dtype = 0;
  std::uint32_t tile_iot_valid = 0;
  std::uint32_t tile_iot_flags = 0;
  std::uint32_t tile_iot_dst = 0;
  std::uint32_t tile_iot_grp = 0;
  std::uint32_t tile_iot_src0 = 0;
  std::uint32_t tile_iot_src1 = 0;
  std::uint32_t tile_iot_reg = 0;
  std::uint32_t tile_iot_size = 0;
  std::uint32_t tile_arg_format = 0;
  std::uint32_t tile_attr_raw = 0;
  std::uint32_t tile_attr_pad = 0;
  std::uint32_t tile_attr_dtype = 0;
  std::uint32_t tile_ior_count = 0;
  std::array<std::uint64_t, kLinxTileMaxIor> tile_ior_desc{};
  std::uint32_t vec_ri_count = 0;
  std::array<std::uint64_t, kLinxVecRiMax> vec_ri_value{};
  std::uint32_t tile_iot_count = 0;
  std::array<std::uint64_t, kLinxTileMaxIot> tile_iot_desc{};
  std::array<std::array<std::uint32_t, kLinxTileMaxWords>, 32> tile_reg{};
  std::array<std::uint32_t, 32> tile_reg_bytes{};
  std::array<std::uint32_t, kLinxTileMaxWords> tile_acc{};
  std::uint32_t tile_acc_bytes = 0;
  SharedTileBank shared_tile{};
  std::uint64_t bpc = 0;
  std::uint64_t insn_pc_next = 0;
  std::uint64_t pc = 0;
  std::uint64_t insn_count = 0;
  std::uint64_t pending_trap_arg0 = 0;
  std::uint32_t pending_trap_cause = 0;
  std::array<std::uint64_t, kLinxAcrCount> irq_level_acr{};
  std::uint64_t lr_addr = 0;
  std::uint32_t lr_size = 0;
  std::uint32_t lr_valid = 0;
  std::string block_kind = "scalar";
  int lane_id = -1;

  void Reset() {
    // Reuse one static zero state instead of materializing a multi-megabyte
    // temporary on the caller's stack for every reset.
    static const LinxState kResetState{};
    *this = kResetState;
  }
};

[[nodiscard]] std::string DumpStateSummary(const LinxState &state);

} // namespace linx::model::emulator
