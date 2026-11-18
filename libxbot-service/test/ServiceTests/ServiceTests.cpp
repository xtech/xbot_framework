//
// Tests for the parts of Service that work without a claimed connection.
//

#include <cstdint>

#include "../TemplateService.hpp"
#include "CppUTest/TestHarness.h"

using namespace xbot::test;

namespace {
constexpr size_t kScratchSize = xbot::config::max_packet_size - sizeof(xbot::datatypes::XbotHeader);
constexpr size_t kDescriptorSize = sizeof(xbot::datatypes::DataDescriptor);

class SendingService : public TemplateService {
 public:
  SendingService() : TemplateService(2) {
  }

  using TemplateService::CommitTransaction;
  using TemplateService::scratch_buffer_fill_;
  using TemplateService::SendData;
  using TemplateService::SendExampleOutputBlob;
  using TemplateService::StartTransaction;
};

const uint8_t data_[kScratchSize]{};
}  // namespace

TEST_GROUP(ServiceTests) {
  SendingService service;

  void setup() override {
    CHECK_TRUE(service.StartTransaction());
  }

  void teardown() override {
    // Nobody claimed the service, so nothing is sent - but the commit releases the transaction lock.
    CHECK_FALSE(service.CommitTransaction());
  }
};

TEST(ServiceTests, TransactionAppendsDescriptorAndData) {
  CHECK_TRUE(service.SendExampleOutputBlob(data_, 100));
  LONGS_EQUAL(kDescriptorSize + 100, service.scratch_buffer_fill_);
  CHECK_TRUE(service.SendExampleOutputBlob(data_, 0));
  LONGS_EQUAL(2 * kDescriptorSize + 100, service.scratch_buffer_fill_);
}

TEST(ServiceTests, TransactionTakesAValueThatFillsThePacket) {
  CHECK_TRUE(service.SendExampleOutputBlob(data_, kScratchSize - kDescriptorSize));
  LONGS_EQUAL(kScratchSize, service.scratch_buffer_fill_);
  // Full: not even an empty value fits, it still needs its descriptor.
  CHECK_FALSE(service.SendExampleOutputBlob(data_, 0));
  LONGS_EQUAL(kScratchSize, service.scratch_buffer_fill_);
}

TEST(ServiceTests, TransactionRejectsAValueThatDoesNotFit) {
  CHECK_FALSE(service.SendExampleOutputBlob(data_, kScratchSize - kDescriptorSize + 1));
  LONGS_EQUAL(0, service.scratch_buffer_fill_);

  CHECK_TRUE(service.SendExampleOutputBlob(data_, 1000));
  CHECK_FALSE(service.SendExampleOutputBlob(data_, 1000));
  LONGS_EQUAL(kDescriptorSize + 1000, service.scratch_buffer_fill_);
}

TEST(ServiceTests, TransactionRejectsASizeThatWrapsTheSum) {
  // fill + size + descriptor wraps to a small number for these; the check must not add them up.
  CHECK_FALSE(service.SendData(2, data_, SIZE_MAX));
  CHECK_FALSE(service.SendData(2, data_, SIZE_MAX - kDescriptorSize + 1));
  CHECK_TRUE(service.SendExampleOutputBlob(data_, 100));
  CHECK_FALSE(service.SendData(2, data_, SIZE_MAX - kDescriptorSize - 100));
  LONGS_EQUAL(kDescriptorSize + 100, service.scratch_buffer_fill_);
}
