//
// Shared buffer pool for RPC payloads.
//

#ifndef XBOT_BUFFER_HPP
#define XBOT_BUFFER_HPP

#include <cstddef>
#include <cstdint>
#include <xbot/config.hpp>
#include <xbot/datatypes/XbotHeader.hpp>

namespace xbot::service::buffer {

/**
 * @brief Smallest block size a pool may have: the largest RPC return value, i.e. one packet without
 * its header.
 *
 * The generated dispatchers borrow up to this many bytes. A pool with smaller blocks could never serve
 * them and would answer BUSY forever, so initialize() refuses it.
 */
static constexpr size_t min_block_size = xbot::config::max_packet_size - sizeof(xbot::datatypes::XbotHeader);

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
 *
 * A block is handed out as it was returned: it still holds what the previous
 * owner wrote. Clear it before use if anything but the bytes written by the new
 * owner may be sent (the generated dispatchers do).
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
 * @param block_size bytes per block, a multiple of block_alignment and at least min_block_size
 * @param block_count number of blocks, 1..max_block_count
 * @return false if an argument is unusable or a lease is still out; the pool then stays as it was
 */
bool initialize(void *storage, size_t block_size, size_t block_count);

/**
 * @brief Borrow a block that holds at least @p bytes.
 *
 * Deliberately non-blocking: an RPC handler must never wait for a slow transfer
 * on another service, it answers BUSY instead.
 *
 * The generated dispatchers borrow the block before they call the handler and
 * return it when the handler returns, so a block is out for as long as the
 * handler runs - not only while the return value is copied into a packet. Size
 * the pool (buffer_block_count) for the number of pooled RPCs that can be in
 * their handlers at the same time.
 *
 * @return An empty lease if the pool has no storage yet, the request is larger
 *         than a block or every block is out.
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
