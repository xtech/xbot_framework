//
// The service generated from codegen/templates/service.json, opened up for the tests.
//

#ifndef XBOT_TEST_TEMPLATE_SERVICE_HPP
#define XBOT_TEST_TEMPLATE_SERVICE_HPP

// Everything the generated header pulls in, so the define below only reaches the generated class.
#include <xbot-service/Buffer.hpp>
#include <xbot-service/Service.hpp>

// dispatchRpcCall() is a private override in the generated class and is only reached through a
// received packet. The dispatcher tests have to call it directly.
#define private public
#include <ServiceTemplateBase.hpp>
#undef private

#include <cstddef>
#include <cstdint>

namespace xbot::test {

// Function ids of codegen/templates/service.json.
enum FunctionId : uint8_t {
  kNoParamsNoReturn = 0,
  kScalarParamsWithReturn = 1,
  kArrayParamNoReturn = 2,
  kMixedParamsWithReturn = 3,
  kBlobParamsWithBlobReturn = 4,
  kLargeArrayReturn = 5,
  kStringParamsWithStringReturn = 6,
  kLargeStringReturn = 7,
};

// The smallest block a buffer pool accepts, rounded up to the block alignment.
constexpr size_t kPoolBlockSize = (xbot::service::buffer::min_block_size + xbot::service::buffer::block_alignment - 1) /
                                  xbot::service::buffer::block_alignment * xbot::service::buffer::block_alignment;

// The template service with every handler stubbed out. A test overrides what it looks at.
class TemplateService : public ServiceTemplateBase {
 public:
  explicit TemplateService(uint16_t service_id) : ServiceTemplateBase(service_id) {
  }

 protected:
  void RPCNoParamsNoReturn(uint16_t) override {
  }
  void RPCScalarParamsWithReturn(uint16_t, float, uint32_t, uint8_t) override {
  }
  void RPCArrayParamNoReturn(uint16_t, const char*, uint32_t) override {
  }
  void RPCMixedParamsWithReturn(uint16_t, const char*, uint32_t, float) override {
  }
  void RPCBlobParamsWithBlobReturn(uint16_t, const uint8_t*, uint32_t, uint8_t*, uint16_t*) override {
  }
  void RPCLargeArrayReturn(uint16_t, uint8_t*, uint16_t*) override {
  }
  void RPCStringParamsWithStringReturn(uint16_t, const char*, uint32_t, char*, uint16_t*) override {
  }
  void RPCLargeStringReturn(uint16_t, char*, uint16_t*) override {
  }
};

}  // namespace xbot::test

#endif  // XBOT_TEST_TEMPLATE_SERVICE_HPP
