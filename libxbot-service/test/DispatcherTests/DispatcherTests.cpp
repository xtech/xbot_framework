//
// Tests for the generated RPC dispatcher, run against the template schema.
//
// The dispatcher is called directly, without a claimed connection. A response the dispatcher or a
// handler sends is therefore dropped, and what a test can observe is whether the handler ran and
// what it was given - which is exactly the contract between the generated code and a service.
//

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "../TemplateService.hpp"
#include "CppUTest/TestHarness.h"

using namespace xbot::service;
using namespace xbot::test;

namespace {
constexpr size_t kBlocks = 2;
constexpr size_t kBlockSize = kPoolBlockSize;
constexpr uint16_t kCallId = 7;

// Static for the same reason as in BufferTests: the pool keeps the pointer.
alignas(buffer::block_alignment) uint8_t pool_[kBlocks * kBlockSize]{};

bool allZero(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  return std::all_of(bytes, bytes + size, [](uint8_t b) { return b == 0; });
}

// An RPC body: one DataDescriptor per parameter, each followed by its bytes.
struct Body {
  std::vector<uint8_t> bytes;

  Body& add(uint16_t id, const void* data, uint32_t size) {
    return addAnnouncing(id, data, size, size);
  }

  // Sends size bytes but announces another length in the descriptor.
  Body& addAnnouncing(uint16_t id, const void* data, uint32_t size, uint32_t announced) {
    xbot::datatypes::DataDescriptor descriptor{};
    descriptor.target_id = id;
    descriptor.payload_size = announced;
    const auto* raw = reinterpret_cast<const uint8_t*>(&descriptor);
    bytes.insert(bytes.end(), raw, raw + sizeof(descriptor));
    const auto* payload = static_cast<const uint8_t*>(data);
    bytes.insert(bytes.end(), payload, payload + size);
    return *this;
  }
};

// Records what the handlers were given.
class RecordingService : public TemplateService {
 public:
  RecordingService() : TemplateService(1) {
  }

  void call(uint8_t function_id, const Body& body) {
    dispatchRpcCall(function_id, kCallId, body.bytes.data(), body.bytes.size());
  }

  int calls = 0;

  // Parameters.
  std::string text;
  uint32_t text_len = 0;
  bool text_terminated = false;
  const uint8_t* blob = nullptr;
  uint32_t blob_len = 0;
  float speed = 0;
  uint32_t count = 0;
  uint8_t enable = 0;

  // Return buffer, as the handler found it.
  void* ret = nullptr;
  uint16_t ret_capacity = 0;
  bool ret_was_zero = false;
  size_t free_blocks_in_handler = 0;

 protected:
  void RPCNoParamsNoReturn(uint16_t) override {
    ++calls;
  }

  void RPCScalarParamsWithReturn(uint16_t, float Speed, uint32_t Count, uint8_t Enable) override {
    ++calls;
    speed = Speed;
    count = Count;
    enable = Enable;
  }

  void RPCBlobParamsWithBlobReturn(uint16_t, const uint8_t* Data, uint32_t DataLen, uint8_t* data,
                                   uint16_t* response_length) override {
    ++calls;
    blob = Data;
    blob_len = DataLen;
    recordReturn(data, *response_length, *response_length);
    // What a handler does with the buffer: this must not reach the next owner of the block.
    memset(data, 0xEE, *response_length);
  }

  void RPCLargeArrayReturn(uint16_t, uint8_t* data, uint16_t* response_length) override {
    ++calls;
    recordReturn(data, *response_length, *response_length);
    memset(data, 0xEE, *response_length);
  }

  void RPCStringParamsWithStringReturn(uint16_t, const char* Path, uint32_t PathLen, char* data,
                                       uint16_t* response_length) override {
    ++calls;
    text.assign(Path, PathLen);
    text_len = PathLen;
    text_terminated = Path[PathLen] == '\0';
    // A string return has one byte more than its limit, for the terminator.
    recordReturn(data, *response_length, static_cast<size_t>(*response_length) + 1);
  }

  void RPCLargeStringReturn(uint16_t, char* data, uint16_t* response_length) override {
    ++calls;
    recordReturn(data, *response_length, static_cast<size_t>(*response_length) + 1);
    memset(data, 'x', *response_length);
  }

 private:
  void recordReturn(void* data, uint16_t capacity, size_t bytes) {
    ret = data;
    ret_capacity = capacity;
    ret_was_zero = allZero(data, bytes);
    free_blocks_in_handler = buffer::available();
  }
};

// Leaves every block of the pool full of 0xA5, like a previous owner would.
void dirtyThePool() {
  buffer::Lease first = buffer::acquire(1);
  buffer::Lease second = buffer::acquire(1);
  memset(first.data(), 0xA5, first.size());
  memset(second.data(), 0xA5, second.size());
}
}  // namespace

TEST_GROUP(DispatcherTests) {
  RecordingService service;

  void setup() override {
    CHECK_TRUE(buffer::initialize(pool_, kBlockSize, kBlocks));
    dirtyThePool();
  }

  void teardown() override {
    // The dispatcher returns its block when the call is over, whatever the outcome.
    LONGS_EQUAL(kBlocks, buffer::available());
  }
};

TEST(DispatcherTests, CallWithoutParameters) {
  service.call(kNoParamsNoReturn, Body{});
  LONGS_EQUAL(1, service.calls);
}

TEST(DispatcherTests, ScalarParametersArriveInAnyOrder) {
  const float speed = 1.5F;
  const uint32_t count = 42;
  const uint8_t enable = 1;
  service.call(kScalarParamsWithReturn,
               Body{}.add(2, &enable, sizeof(enable)).add(0, &speed, sizeof(speed)).add(1, &count, sizeof(count)));
  LONGS_EQUAL(1, service.calls);
  DOUBLES_EQUAL(1.5, service.speed, 0.0);
  LONGS_EQUAL(42, service.count);
  LONGS_EQUAL(1, service.enable);
}

TEST(DispatcherTests, UnknownParameterIsSkipped) {
  const float speed = 1.5F;
  const uint32_t count = 42;
  const uint8_t enable = 1;
  const uint32_t unknown = 0xDEADBEEF;
  service.call(kScalarParamsWithReturn, Body{}
                                            .add(0, &speed, sizeof(speed))
                                            .add(99, &unknown, sizeof(unknown))
                                            .add(1, &count, sizeof(count))
                                            .add(2, &enable, sizeof(enable)));
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(42, service.count);
}

TEST(DispatcherTests, MissingParameterIsRejected) {
  const float speed = 1.5F;
  const uint32_t count = 42;
  service.call(kScalarParamsWithReturn, Body{}.add(0, &speed, sizeof(speed)).add(1, &count, sizeof(count)));
  LONGS_EQUAL(0, service.calls);
}

TEST(DispatcherTests, DuplicateParameterIsRejected) {
  const float speed = 1.5F;
  const uint32_t count = 42;
  const uint8_t enable = 1;
  service.call(kScalarParamsWithReturn, Body{}
                                            .add(0, &speed, sizeof(speed))
                                            .add(0, &speed, sizeof(speed))
                                            .add(1, &count, sizeof(count))
                                            .add(2, &enable, sizeof(enable)));
  LONGS_EQUAL(0, service.calls);
}

TEST(DispatcherTests, WrongScalarSizeIsRejected) {
  const float speed = 1.5F;
  const uint16_t count = 42;  // the schema says uint32_t
  const uint8_t enable = 1;
  service.call(kScalarParamsWithReturn,
               Body{}.add(0, &speed, sizeof(speed)).add(1, &count, sizeof(count)).add(2, &enable, sizeof(enable)));
  LONGS_EQUAL(0, service.calls);
}

TEST(DispatcherTests, UnknownFunctionIsRejected) {
  service.call(200, Body{});
  LONGS_EQUAL(0, service.calls);
}

TEST(DispatcherTests, StringParameterIsTerminatedAndCounted) {
  service.call(kStringParamsWithStringReturn, Body{}.add(0, "/sounds/a.mp3", 13));
  LONGS_EQUAL(1, service.calls);
  STRCMP_EQUAL("/sounds/a.mp3", service.text.c_str());
  LONGS_EQUAL(13, service.text_len);
  CHECK_TRUE(service.text_terminated);
}

TEST(DispatcherTests, EmptyStringParameterIsTerminated) {
  service.call(kStringParamsWithStringReturn, Body{}.add(0, "", 0));
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(0, service.text_len);
  CHECK_TRUE(service.text_terminated);
}

TEST(DispatcherTests, StringParameterMayUseTheWholeLimit) {
  const std::string path(64, 'p');  // max_length of Path
  service.call(kStringParamsWithStringReturn, Body{}.add(0, path.data(), 64));
  LONGS_EQUAL(1, service.calls);
  STRCMP_EQUAL(path.c_str(), service.text.c_str());
  CHECK_TRUE(service.text_terminated);
}

TEST(DispatcherTests, StringParameterAboveTheLimitIsRejected) {
  const std::string path(65, 'p');
  service.call(kStringParamsWithStringReturn, Body{}.add(0, path.data(), 65));
  LONGS_EQUAL(0, service.calls);  // rejected, not shortened
}

TEST(DispatcherTests, StringReturnBufferIsClearedAndHoldsTheTerminator) {
  service.call(kStringParamsWithStringReturn, Body{}.add(0, "a", 1));
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(96, service.ret_capacity);  // max_length of the return; the buffer has one byte more
  CHECK_TRUE(service.ret_was_zero);
  LONGS_EQUAL(kBlocks, service.free_blocks_in_handler);  // small enough for the stack
}

TEST(DispatcherTests, BlobParameterPointsIntoThePacket) {
  const uint8_t data[] = {1, 2, 3, 4, 5};
  const Body body = Body{}.add(0, data, sizeof(data));
  service.call(kBlobParamsWithBlobReturn, body);
  LONGS_EQUAL(1, service.calls);
  POINTERS_EQUAL(body.bytes.data() + sizeof(xbot::datatypes::DataDescriptor), service.blob);
  LONGS_EQUAL(sizeof(data), service.blob_len);
}

TEST(DispatcherTests, EmptyBlobParameterIsAccepted) {
  service.call(kBlobParamsWithBlobReturn, Body{}.add(0, nullptr, 0));
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(0, service.blob_len);
}

TEST(DispatcherTests, DescriptorLongerThanThePacketIsRejected) {
  const uint8_t data[] = {1, 2, 3, 4};
  // One byte more than is there, and a length that wraps "offset + payload_size" where size_t has 32 bits.
  service.call(kBlobParamsWithBlobReturn, Body{}.addAnnouncing(0, data, sizeof(data), sizeof(data) + 1));
  service.call(kBlobParamsWithBlobReturn, Body{}.addAnnouncing(0, data, sizeof(data), 0xFFFFFFF0U));
  LONGS_EQUAL(0, service.calls);
}

TEST(DispatcherTests, BlobReturnIsAClearedBlockOfOnePacket) {
  service.call(kBlobParamsWithBlobReturn, Body{}.add(0, "x", 1));
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(buffer::min_block_size, service.ret_capacity);
  CHECK_TRUE(service.ret_was_zero);  // not the 0xA5 the previous owner left behind
  LONGS_EQUAL(kBlocks - 1, service.free_blocks_in_handler);
  CHECK_TRUE(service.ret == pool_ || service.ret == pool_ + kBlockSize);
}

TEST(DispatcherTests, LargeArrayReturnIsACleared_PooledBuffer) {
  service.call(kLargeArrayReturn, Body{});
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(256, service.ret_capacity);
  CHECK_TRUE(service.ret_was_zero);
  LONGS_EQUAL(kBlocks - 1, service.free_blocks_in_handler);
}

TEST(DispatcherTests, LargeStringReturnIsACleared_PooledBuffer) {
  service.call(kLargeStringReturn, Body{});
  LONGS_EQUAL(1, service.calls);
  LONGS_EQUAL(200, service.ret_capacity);
  CHECK_TRUE(service.ret_was_zero);  // including the byte for the terminator
  LONGS_EQUAL(kBlocks - 1, service.free_blocks_in_handler);
}

TEST(DispatcherTests, PooledReturnIsClearedOnEveryCall) {
  // The handler fills the buffer; the next call must not see that either.
  service.call(kLargeArrayReturn, Body{});
  service.call(kLargeArrayReturn, Body{});
  service.call(kLargeStringReturn, Body{});
  LONGS_EQUAL(3, service.calls);
  CHECK_TRUE(service.ret_was_zero);
}

TEST(DispatcherTests, PooledReturnIsRefusedWhileNoBlockIsFree) {
  {
    buffer::Lease first = buffer::acquire(1);
    buffer::Lease second = buffer::acquire(1);
    service.call(kLargeArrayReturn, Body{});
    service.call(kBlobParamsWithBlobReturn, Body{}.add(0, "x", 1));
    LONGS_EQUAL(0, service.calls);  // answered BUSY, the handler never ran

    // A return that fits on the stack does not depend on the pool.
    service.call(kStringParamsWithStringReturn, Body{}.add(0, "a", 1));
    LONGS_EQUAL(1, service.calls);
  }
  service.call(kLargeArrayReturn, Body{});
  LONGS_EQUAL(2, service.calls);
}
