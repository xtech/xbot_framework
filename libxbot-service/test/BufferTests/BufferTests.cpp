//
// Tests for the shared RPC buffer pool.
//

#include <cstring>
#include <utility>
#include <xbot-service/Buffer.hpp>

#include "CppUTest/TestHarness.h"

using namespace xbot::service;

namespace {
constexpr size_t kBlocks = 2;
// The smallest block a pool accepts, rounded up to the block alignment.
constexpr size_t kBlockSize =
    (buffer::min_block_size + buffer::block_alignment - 1) / buffer::block_alignment * buffer::block_alignment;

// The tests bring their own storage, so they do not depend on the framework
// default pool. It is static on purpose: initialize() keeps the pointer, and a
// member of the test group would dangle for every test that runs afterwards.
alignas(buffer::block_alignment) uint8_t storage_[kBlocks * kBlockSize]{};
}  // namespace

TEST_GROUP(BufferTests) {
  void setup() override {
    CHECK_TRUE(buffer::initialize(storage_, kBlockSize, kBlocks));
    CHECK_EQUAL(kBlocks, buffer::available());
  }

  void teardown() override {
    // Every lease of a test is gone by now; a leaked one would poison the next test.
    CHECK_EQUAL(kBlocks, buffer::available());
  }
};

TEST(BufferTests, AcquireReturnsBlockCapacity) {
  buffer::Lease lease = buffer::acquire(100);
  CHECK_TRUE(lease);
  CHECK_EQUAL(kBlockSize, lease.size());
  CHECK_TRUE(lease.data() == storage_);
  CHECK_EQUAL(kBlocks - 1, buffer::available());
}

TEST(BufferTests, AcquireUsesDistinctBlocks) {
  buffer::Lease first = buffer::acquire(10);
  buffer::Lease second = buffer::acquire(10);
  CHECK_TRUE(first);
  CHECK_TRUE(second);
  CHECK_TRUE(first.data() != second.data());
  CHECK_EQUAL(static_cast<size_t>(0), buffer::available());
}

TEST(BufferTests, ExhaustedPoolReturnsEmptyLease) {
  buffer::Lease first = buffer::acquire(10);
  buffer::Lease second = buffer::acquire(10);
  buffer::Lease third = buffer::acquire(10);
  CHECK_TRUE(first);
  CHECK_TRUE(second);
  CHECK_FALSE(third);  // the caller answers BUSY
}

TEST(BufferTests, ReleaseReturnsBlock) {
  {
    buffer::Lease lease = buffer::acquire(10);
    CHECK_TRUE(lease);
    lease.release();
    CHECK_FALSE(lease);
    CHECK_EQUAL(kBlocks, buffer::available());
  }
  buffer::Lease again = buffer::acquire(10);
  CHECK_TRUE(again);
}

TEST(BufferTests, DestructorReleasesBlock) {
  {
    buffer::Lease lease = buffer::acquire(10);
    CHECK_TRUE(lease);
  }
  CHECK_EQUAL(kBlocks, buffer::available());
}

TEST(BufferTests, OversizedRequestReturnsEmptyLease) {
  CHECK_FALSE(buffer::acquire(kBlockSize + 1));
  CHECK_EQUAL(kBlocks, buffer::available());
}

TEST(BufferTests, MoveTransfersOwnership) {
  buffer::Lease source = buffer::acquire(10);
  CHECK_TRUE(source);

  buffer::Lease target = std::move(source);
  CHECK_FALSE(source);
  CHECK_TRUE(target);
  CHECK_EQUAL(kBlocks - 1, buffer::available());

  target = std::move(source);  // move assign from an empty lease releases target
  CHECK_FALSE(target);
  CHECK_EQUAL(kBlocks, buffer::available());
}

TEST(BufferTests, ReleaseIsIdempotent) {
  buffer::Lease lease = buffer::acquire(10);
  lease.release();
  lease.release();
  CHECK_EQUAL(kBlocks, buffer::available());
}

TEST(BufferTests, InitializeRejectsBadArguments) {
  CHECK_FALSE(buffer::initialize(nullptr, kBlockSize, kBlocks));
  CHECK_FALSE(buffer::initialize(storage_, 0, kBlocks));
  CHECK_FALSE(buffer::initialize(storage_, kBlockSize, 0));
  CHECK_FALSE(buffer::initialize(storage_, kBlockSize, buffer::max_block_count + 1));
  CHECK_FALSE(buffer::initialize(storage_ + 1, kBlockSize, kBlocks));   // unaligned storage
  CHECK_FALSE(buffer::initialize(storage_, kBlockSize + 1, kBlocks));   // unaligned block stride
}

TEST(BufferTests, InitializeRejectsBlocksBelowOneReturnValue) {
  // The generated dispatchers borrow up to min_block_size bytes. A pool that
  // cannot serve that would answer BUSY forever, so it is refused up front.
  alignas(buffer::block_alignment) static uint8_t small[2 * 512];
  CHECK_FALSE(buffer::initialize(small, 512, 2));
  CHECK_FALSE(buffer::initialize(storage_, kBlockSize - buffer::block_alignment, kBlocks));

  // A refused call leaves the pool as it was.
  CHECK_EQUAL(kBlockSize, buffer::blockSize());
  CHECK_EQUAL(kBlocks, buffer::blockCount());
  buffer::Lease lease = buffer::acquire(buffer::min_block_size);
  CHECK_TRUE(lease);
  CHECK_TRUE(lease.data() == storage_);
}

TEST(BufferTests, ReacquiredBlockKeepsThePreviousContent) {
  // Documented contract: acquire() does not clear a block. The generated
  // dispatchers rely on knowing that and clear the bytes they hand out.
  {
    buffer::Lease lease = buffer::acquire(16);
    CHECK_TRUE(lease);
    memset(lease.data(), 0xA5, 16);
  }
  buffer::Lease again = buffer::acquire(16);
  CHECK_TRUE(again);
  CHECK_TRUE(again.data() == storage_);
  BYTES_EQUAL(0xA5, again.data()[0]);
  BYTES_EQUAL(0xA5, again.data()[15]);
}

TEST(BufferTests, InitializeRefusesWhileALeaseIsOut) {
  buffer::Lease lease = buffer::acquire(10);
  CHECK_TRUE(lease);
  CHECK_FALSE(buffer::initialize(storage_, kBlockSize, kBlocks));
  lease.release();
  CHECK_TRUE(buffer::initialize(storage_, kBlockSize, kBlocks));
}

TEST(BufferTests, BlockSizeAndCountReportTheInitializedPool) {
  CHECK_EQUAL(kBlockSize, buffer::blockSize());
  CHECK_EQUAL(kBlocks, buffer::blockCount());
}
