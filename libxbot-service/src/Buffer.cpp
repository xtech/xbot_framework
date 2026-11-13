//
// Shared buffer pool for RPC payloads.
//

#include <ulog.h>

#include <atomic>
#include <cstdint>
#include <xbot-service/Buffer.hpp>
#include <xbot/config.hpp>
#include <xbot/datatypes/XbotHeader.hpp>

namespace {

using xbot::service::buffer::invalid_index;
using xbot::service::buffer::Lease;

// A return value has to fit into one packet, so a block never has to be larger
// than that. Keeping the invariant here catches a pool that was sized for
// something smaller.
static_assert(xbot::config::buffer_block_size >= xbot::config::max_packet_size - sizeof(xbot::datatypes::XbotHeader),
              "buffer_block_size must hold the largest possible RPC return value");
static_assert(xbot::config::buffer_block_count >= 1U, "the buffer pool needs at least one block");
static_assert(xbot::config::buffer_block_count <= xbot::service::buffer::max_block_count,
              "buffer_block_count exceeds the pool bitmap");

#if XBOT_BUFFER_DEFAULT_POOL
// Storage for platforms that do not bring their own (hosts, tests, examples). A
// firmware that places the pool in its own RAM section sets
// XBOT_BUFFER_DEFAULT_POOL to 0 and calls buffer::initialize() instead.
alignas(xbot::service::buffer::block_alignment) uint8_t
    default_storage[xbot::config::buffer_block_count * xbot::config::buffer_block_size]{};
#endif

// Constant initialized on purpose: acquire() may run before a dynamic
// initializer had a chance to run (services start right after the IO thread).
#if XBOT_BUFFER_DEFAULT_POOL
uint8_t *storage_ = default_storage;
size_t block_size_ = xbot::config::buffer_block_size;
size_t block_count_ = xbot::config::buffer_block_count;
#else
uint8_t *storage_ = nullptr;
size_t block_size_ = 0;
size_t block_count_ = 0;
#endif

// One bit per block, set while the block is out. The compare-and-swap loop keeps
// the pool lock free: no mutex means no priority inversion, and - more
// importantly - no port has to provide a try-lock, so every existing port keeps
// working unchanged.
std::atomic<uint32_t> used_mask_{0};

// Guards the "not initialized" log line only, so a plain bool is enough: the
// worst case is a duplicated line.
bool warned_uninitialized_ = false;

uint32_t blockBit(uint8_t index) {
  return uint32_t{1} << index;
}

}  // namespace

Lease::Lease(Lease &&other) noexcept : data_(other.data_), size_(other.size_), index_(other.index_) {
  other.data_ = nullptr;
  other.size_ = 0;
  other.index_ = invalid_index;
}

Lease &Lease::operator=(Lease &&other) noexcept {
  if (this != &other) {
    release();
    data_ = other.data_;
    size_ = other.size_;
    index_ = other.index_;
    other.data_ = nullptr;
    other.size_ = 0;
    other.index_ = invalid_index;
  }
  return *this;
}

Lease::~Lease() {
  release();
}

void Lease::release() {
  if (data_ == nullptr) {
    return;
  }
  // Release: everything the handler wrote into the block is visible before the
  // next owner reads it.
  used_mask_.fetch_and(~blockBit(index_), std::memory_order_release);
  data_ = nullptr;
  size_ = 0;
  index_ = invalid_index;
}

bool xbot::service::buffer::initialize(void *storage, size_t block_size, size_t block_count) {
  if (storage == nullptr || block_size == 0U || block_count == 0U) {
    return false;
  }
  if (block_count > max_block_count) {
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(storage) % block_alignment) != 0U) {
    return false;
  }
  if (used_mask_.load(std::memory_order_relaxed) != 0U) {
    return false;  // a lease is still out, keep the current storage alive
  }
  storage_ = static_cast<uint8_t *>(storage);
  block_size_ = block_size;
  block_count_ = block_count;
  ULOG_INFO("Buffer pool: %u blocks of %u bytes", static_cast<unsigned>(block_count),
            static_cast<unsigned>(block_size));
  return true;
}

xbot::service::buffer::Lease xbot::service::buffer::acquire(size_t bytes) {
  if (storage_ == nullptr) {
    if (!warned_uninitialized_) {
      warned_uninitialized_ = true;
      ULOG_WARNING("Buffer pool is not initialized, call xbot::service::buffer::initialize()");
    }
    return {};
  }
  if (bytes > block_size_) {
    return {};
  }

  uint32_t mask = used_mask_.load(std::memory_order_relaxed);
  for (;;) {
    uint8_t index = invalid_index;
    for (uint8_t i = 0; i < block_count_; ++i) {
      if ((mask & blockBit(i)) == 0U) {
        index = i;
        break;
      }
    }
    if (index == invalid_index) {
      return {};  // every block is out, the caller answers BUSY
    }
    if (used_mask_.compare_exchange_weak(mask, mask | blockBit(index), std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
      return {storage_ + static_cast<size_t>(index) * block_size_, block_size_, index};
    }
    // compare_exchange_weak refreshed mask with the current value, try again
  }
}

size_t xbot::service::buffer::blockSize() {
  return block_size_;
}

size_t xbot::service::buffer::blockCount() {
  return block_count_;
}

size_t xbot::service::buffer::available() {
  size_t used = 0;
  const uint32_t mask = used_mask_.load(std::memory_order_relaxed);
  for (uint8_t i = 0; i < block_count_; ++i) {
    if ((mask & blockBit(i)) != 0U) {
      ++used;
    }
  }
  return block_count_ - used;
}
