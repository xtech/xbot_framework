//
// Checks on the interface generated from codegen/templates/service.json.
//
// The frame budget helpers are constexpr, so those checks are static_asserts: they pass by
// compiling. Linking this file is what builds the generated interface source. main() then runs the
// generated Call...() methods against a fake network and checks what GetLastRpcResult() reports.
//

#include <ServiceTemplateInterfaceBase.hpp>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using xbot::datatypes::DataDescriptor;
using xbot::datatypes::XbotHeader;

constexpr size_t kBody = xbot::config::max_packet_size - sizeof(XbotHeader);

// RemainingBudget is protected: it exists for the generated helpers.
struct Budget : xbot::serviceif::ServiceInterfaceBase {
  using ServiceInterfaceBase::RemainingBudget;
};

// A saturating subtraction, for any number of subtrahends.
static_assert(Budget::RemainingBudget(10, 3) == 7);
static_assert(Budget::RemainingBudget(10, 10) == 0);
static_assert(Budget::RemainingBudget(10, 11) == 0);
static_assert(Budget::RemainingBudget(100, 10, 20, 30) == 40);
static_assert(Budget::RemainingBudget(100, 10, 200, 30) == 0);  // stays at 0, does not wrap back up

// A blob parameter may use what one packet has left after the framing of its call.
static_assert(ServiceTemplateInterfaceBase::MaxBlobParamsWithBlobReturnData() == kBody - sizeof(DataDescriptor));

// A string parameter is limited by its max_length (64) as well: Call...() refuses a longer value,
// so a helper that reported the packet budget would send the caller into a call that always fails.
static_assert(ServiceTemplateInterfaceBase::MaxStringParamsWithStringReturnPath() == 64);

using xbot::serviceif::ServiceInterfaceBase;
using xbot::datatypes::RpcStatus;

constexpr uint16_t kServiceId = 7;

// Finds the service under kServiceId, with the type and version of the generated interface.
class FakeDiscovery final : public xbot::serviceif::ServiceDiscovery {
 public:
  void RegisterCallbacks(xbot::serviceif::ServiceDiscoveryCallbacks *) override {
  }
  void UnregisterCallbacks(xbot::serviceif::ServiceDiscoveryCallbacks *) override {
  }
  std::unique_ptr<xbot::serviceif::ServiceInfo> GetServiceInfo(uint16_t service_id) override {
    auto info = std::make_unique<xbot::serviceif::ServiceInfo>();
    info->service_id_ = service_id;
    info->description.type = "ServiceTemplate";
    info->description.version = 1;
    return info;
  }
};

// Stands in for the network: answers every RPC call with `status` and an empty payload. The answer
// comes from another thread, like the real receive path (SendRpc holds its lock while it sends).
class FakeIO final : public xbot::serviceif::ServiceIO {
 public:
  RpcStatus status = RpcStatus::SUCCESS;

  ~FakeIO() {
    Join();
  }
  void RegisterCallbacks(uint16_t, xbot::serviceif::ServiceIOCallbacks *callbacks) override {
    callbacks_ = callbacks;
  }
  void UnregisterCallbacks(xbot::serviceif::ServiceIOCallbacks *) override {
    callbacks_ = nullptr;
  }
  bool SendData(uint16_t service_id, const std::vector<uint8_t> &data) override {
    XbotHeader header{};
    memcpy(&header, data.data(), sizeof(header));
    Join();
    reply_ = std::thread([this, service_id, call_id = static_cast<uint16_t>(header.arg2), reply_status = status] {
      callbacks_->OnRpcResponse(service_id, call_id, static_cast<uint8_t>(reply_status), nullptr, 0);
    });
    return true;
  }
  bool OK() override {
    return true;
  }

 private:
  void Join() {
    if (reply_.joinable()) reply_.join();
  }

  xbot::serviceif::ServiceIOCallbacks *callbacks_ = nullptr;
  std::thread reply_;
};

int failures = 0;

void Expect(bool ok, const char *what) {
  if (!ok) {
    std::fprintf(stderr, "FAILED: %s\n", what);
    ++failures;
  }
}

// Brings the last result to RPC_BUSY, so a later check can tell a fresh result from a stale one.
void MakeBusy(FakeIO &io, ServiceTemplateInterfaceBase &iface) {
  io.status = RpcStatus::BUSY;
  Expect(!iface.CallNoParamsNoReturn(), "a BUSY answer fails the call");
  Expect(iface.GetLastRpcResult() == ServiceInterfaceBase::RPC_BUSY, "a BUSY answer is reported as RPC_BUSY");
  io.status = RpcStatus::SUCCESS;
}
}  // namespace

int main() {
  // Declared before the interface: its destructor unregisters from both.
  FakeDiscovery discovery;
  FakeIO io;
  ServiceTemplateInterfaceBase iface(kServiceId, {&io, &discovery, nullptr});
  Expect(iface.OnServiceDiscovered(kServiceId), "the fake service is discovered");

  Expect(iface.CallNoParamsNoReturn(), "a SUCCESS answer passes the call");
  Expect(iface.GetLastRpcResult() == ServiceInterfaceBase::RPC_OK, "a SUCCESS answer is reported as RPC_OK");

  // Refused before sending: a string over its max_length (64), and a null string. Neither may leave
  // the BUSY of the previous call behind, which would tell the caller to send it again.
  std::string result;
  const std::string too_long(65, 'x');
  MakeBusy(io, iface);
  Expect(!iface.CallStringParamsWithStringReturn(too_long.c_str(), result), "a string over max_length is refused");
  Expect(iface.GetLastRpcResult() == ServiceInterfaceBase::RPC_ERROR, "a string over max_length is RPC_ERROR");

  MakeBusy(io, iface);
  Expect(!iface.CallStringParamsWithStringReturn(nullptr, result), "a null string is refused");
  Expect(iface.GetLastRpcResult() == ServiceInterfaceBase::RPC_ERROR, "a null string is RPC_ERROR");

  // Refused after the answer: SendRpc saw a SUCCESS, but an empty payload is no int32_t.
  int32_t value = 0;
  MakeBusy(io, iface);
  Expect(!iface.CallScalarParamsWithReturn(1.0f, 2, 3, value), "an empty answer to a scalar return is refused");
  Expect(iface.GetLastRpcResult() == ServiceInterfaceBase::RPC_ERROR, "an empty answer to a scalar return is RPC_ERROR");

  return failures == 0 ? 0 : 1;
}
