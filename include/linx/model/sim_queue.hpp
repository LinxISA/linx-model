#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "linx/model/packet_dump.hpp"
#include "linx/model/sim_assert.hpp"
#include "linx/model/sim_object.hpp"

namespace linx::model {

namespace detail {

template <class T> struct SmartPtrTraits {
  static constexpr bool is_unique = false;
  static constexpr bool is_shared = false;
};

template <class T, class Deleter> struct SmartPtrTraits<std::unique_ptr<T, Deleter>> {
  using element_type = T;
  static constexpr bool is_unique = true;
  static constexpr bool is_shared = false;
};

template <class T> struct SmartPtrTraits<std::shared_ptr<T>> {
  using element_type = T;
  static constexpr bool is_unique = false;
  static constexpr bool is_shared = true;
};

} // namespace detail

/**
 * @brief Queue building block for cycle-accurate models.
 *
 * Semantics:
 * - Writes land in the write side immediately.
 * - Each write captures the queue latency at enqueue time.
 * - Work() decrements every pending delay once per cycle and then promotes the
 *   oldest ready entries to the read side.
 * - latency == N means a write becomes readable after exactly N Work() calls.
 * - latency == 0 is still synchronized to the next Work() boundary; this queue
 *   does not model same-cycle combinational visibility.
 * - Payloads are moved through the queue. This supports plain value payloads
 *   (e.g. bool, int, enums, small structs) as well as move-only types such as
 *   unique_ptr for wide pipelines that must avoid object copies.
 */
template <class T> class SimQueue : public SimObject {
public:
  using value_type = T;
  using size_type = std::size_t;
  using payload_type = T;
  static constexpr bool stores_unique_ptr = detail::SmartPtrTraits<T>::is_unique;
  static constexpr bool stores_shared_ptr = detail::SmartPtrTraits<T>::is_shared;
  static constexpr bool stores_smart_ptr = stores_unique_ptr || stores_shared_ptr;

  explicit SimQueue(size_type max_size = 40960, std::uint32_t latency = 1,
                    std::string module_name = "sim_queue")
      : SimObject(std::move(module_name)), maxQSize_(max_size), latency_(latency) {
    static_assert(std::is_move_constructible_v<T>, "SimQueue payload must be move constructible");
    LINX_MODEL_ASSERT(maxQSize_ > 0);
  }

  [[nodiscard]] bool IsQueueBased() const noexcept override {
    return true;
  }

  void Work() override {
    if (stalled_) {
      return;
    }

    bool promoted = false;
    for (auto &delay : delayQ_) {
      if (delay > 0) {
        --delay;
      }
    }

    while (!writeQ_.empty()) {
      LINX_MODEL_ASSERT(writeQ_.size() == delayQ_.size());
      if (delayQ_.front() != 0) {
        break;
      }

      readQ_.push_back(std::move(writeQ_.front()));
      writeQ_.pop_front();
      delayQ_.pop_front();
      promoted = true;
    }

    if (promoted) {
      ++visible_epoch_;
    }
  }

  void Reset() override {
    writeQ_.clear();
    readQ_.clear();
    delayQ_.clear();
    stalled_ = false;
    visible_epoch_ = 0;
  }

  void SetLatency(std::uint32_t latency) noexcept {
    latency_ = latency;
  }
  std::uint32_t GetLatency() const noexcept {
    return latency_;
  }

  bool GetStall() const noexcept {
    return stalled_;
  }
  void SetStall() noexcept {
    stalled_ = true;
  }
  void SetStall(bool stalled) noexcept {
    stalled_ = stalled;
  }
  void UnsetStall() noexcept {
    stalled_ = false;
  }

  template <typename... Args> T &Write(Args &&...args) {
    LINX_MODEL_ASSERT(!Full());
    writeQ_.emplace_back(std::forward<Args>(args)...);
    delayQ_.push_back(latency_);
    LINX_MODEL_ASSERT(writeQ_.size() == delayQ_.size());
    return writeQ_.back();
  }

  template <class... Args, class U = T>
  std::enable_if_t<detail::SmartPtrTraits<U>::is_unique, T &> EmplaceUnique(Args &&...args) {
    using Pointee = typename detail::SmartPtrTraits<U>::element_type;
    return Write(std::make_unique<Pointee>(std::forward<Args>(args)...));
  }

  template <class... Args, class U = T>
  std::enable_if_t<detail::SmartPtrTraits<U>::is_shared, T &> EmplaceShared(Args &&...args) {
    using Pointee = typename detail::SmartPtrTraits<U>::element_type;
    return Write(std::make_shared<Pointee>(std::forward<Args>(args)...));
  }

  T Read() {
    LINX_MODEL_ASSERT(!readQ_.empty());
    T result = std::move(readQ_.front());
    readQ_.pop_front();
    ++visible_epoch_;
    return result;
  }

  T &Front() {
    LINX_MODEL_ASSERT(!readQ_.empty());
    return readQ_.front();
  }

  const T &Front() const {
    LINX_MODEL_ASSERT(!readQ_.empty());
    return readQ_.front();
  }

  void Pop() {
    LINX_MODEL_ASSERT(!readQ_.empty());
    readQ_.pop_front();
    ++visible_epoch_;
  }

  bool Empty() const noexcept {
    return readQ_.empty();
  }
  bool EmptyW() const noexcept {
    return writeQ_.empty();
  }

  bool Full() const noexcept {
    return Occupancy() >= maxQSize_;
  }

  bool Full(size_type reserve) const noexcept {
    return WillBeFull(reserve);
  }

  bool WillBeFull(size_type reserve = 0) const noexcept {
    if (reserve > maxQSize_) {
      return true;
    }
    return Occupancy() >= (maxQSize_ - reserve);
  }

  size_type Size() const noexcept {
    return readQ_.size();
  }
  size_type SizeW() const noexcept {
    return writeQ_.size();
  }
  size_type Occupancy() const noexcept {
    return readQ_.size() + writeQ_.size();
  }
  size_type MaxSize() const noexcept {
    return maxQSize_;
  }

  void InitMaxSize(size_type max_size) {
    LINX_MODEL_ASSERT(max_size > 0);
    LINX_MODEL_ASSERT(Occupancy() <= max_size);
    maxQSize_ = max_size;
  }

  std::deque<T> &GetRawWriteData() noexcept {
    return writeQ_;
  }
  const std::deque<T> &GetRawWriteData() const noexcept {
    return writeQ_;
  }

  std::deque<T> &GetRawReadData() noexcept {
    return readQ_;
  }
  const std::deque<T> &GetRawReadData() const noexcept {
    return readQ_;
  }

  const std::deque<std::uint32_t> &GetRawDelayData() const noexcept {
    return delayQ_;
  }

  [[nodiscard]] std::uint64_t VisibleEpoch() const noexcept {
    return visible_epoch_;
  }

  template <class Predicate> size_type FlushIf(Predicate &&match) {
    size_type removed = 0;
    bool removed_visible = false;

    for (size_type i = 0; i < writeQ_.size();) {
      if (std::invoke(match, writeQ_[i])) {
        writeQ_.erase(writeQ_.begin() + static_cast<std::ptrdiff_t>(i));
        delayQ_.erase(delayQ_.begin() + static_cast<std::ptrdiff_t>(i));
        ++removed;
      } else {
        ++i;
      }
    }

    for (auto it = readQ_.begin(); it != readQ_.end();) {
      if (std::invoke(match, *it)) {
        it = readQ_.erase(it);
        ++removed;
        removed_visible = true;
      } else {
        ++it;
      }
    }

    LINX_MODEL_ASSERT(writeQ_.size() == delayQ_.size());
    if (removed_visible) {
      ++visible_epoch_;
    }
    return removed;
  }

private:
  size_type maxQSize_;
  std::deque<T> writeQ_;
  std::deque<T> readQ_;
  std::deque<std::uint32_t> delayQ_;
  std::uint32_t latency_;
  bool stalled_ = false;
  std::uint64_t visible_epoch_ = 0;
};

template <class T> using SimUniqueQueue = SimQueue<std::unique_ptr<T>>;

template <class T> using SimSharedQueue = SimQueue<std::shared_ptr<T>>;

} // namespace linx::model
