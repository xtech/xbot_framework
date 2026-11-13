//
// Shared buffer pool for RPC payloads.
//

#ifndef XBOT_BUFFER_HPP
#define XBOT_BUFFER_HPP

#include <cstddef>
#include <cstdint>

namespace xbot::service::buffer {

/** @brief Highest number of blocks the pool bitmap can hold. */
static constexpr uint8_t max_block_count = 32;

/** @brief Alignment of every block and of the storage passed to initialize(). */
static constexpr size_t block_alignment = 8;

/** @brief Index that marks "no block". */
static constexpr uint8_t invalid_index = 0xFF;

/**
 * @brief A block borrowed from the shared buffer pool.
 *
 * A lease owns one block until it is released or destroyed. It is move-only, so
 * a block can never be released twice and the destructor returns it to the pool.
 *
 * Do not keep a lease beyond the call that borrowed it. The pool is shared by
 * all services: a lease that is held across a flash erase or across RPCs starves
 * the next caller, which then has to answer BUSY.
 */
class Lease {
 public:
  Lease() = default;
  Lease(const Lease &) = delete;
  Lease &operator=(const Lease &) = delete;
  Lease(Lease &&other) noexcept;
  Lease &operator=(Lease &&other) noexcept;
  ~Lease();

  /** @brief Start of the block, nullptr for an empty lease. */
  uint8_t *data() const {
    return data_;
  }

  /** @brief Capacity of the block in bytes (block size, not the requested size). */
  size_t size() const {
    return size_;
  }

  /** @brief True if this lease owns a block. */
  explicit operator bool() const {
    return data_ != nullptr;
  }

  /** @brief Return the block to the pool. Idempotent. */
  void release();

 private:
  friend Lease acquire(size_t bytes);
  Lease(uint8_t *data, size_t size, uint8_t index) : data_(data), size_(size), index_(index) {
  }

  uint8_t *data_ = nullptr;
  size_t size_ = 0;
  uint8_t index_ = invalid_index;
};

/**
 * @brief Register the storage of the pool.
 *
 * Call once, before the first acquire() and before the services start; it is not
 * thread safe. A platform that brings its own storage (e.g. a section in RAM that
 * is otherwise unused) calls this with that storage and sets
 * XBOT_BUFFER_DEFAULT_POOL to 0, which removes the framework default array.
 *
 * @param storage    start of the storage, block_alignment aligned
 * @param block_size bytes per block, must hold the largest RPC return value
 * @param block_count number of blocks, 1..max_block_count
 * @return false if an argument is unusable or a lease is still out
 */
bool initialize(void *storage, size_t block_size, size_t block_count);

/**
 * @brief Borrow a block that holds at least @p bytes.
 *
 * Deliberately non-blocking: an RPC handler must never wait for a slow transfer
 * on another service, it answers BUSY instead. A timeout would not help either,
 * because a block is only held while a return value is filled and copied into a
 * packet, i.e. for microseconds, not for the duration of an upload.
 *
 * @return An empty lease if the request is larger than a block or every block is
 *         out.
 */
Lease acquire(size_t bytes);

/** @brief Block size in bytes. */
size_t blockSize();

/** @brief Number of blocks in the pool. */
size_t blockCount();

/** @brief Number of blocks that are currently free. */
size_t available();

}  // namespace xbot::service::buffer

#endif  // XBOT_BUFFER_HPP
