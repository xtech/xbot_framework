//
// Created by clemens on 7/17/24.
//

#ifndef SERVICEINTERFACEBASE_HPP
#define SERVICEINTERFACEBASE_HPP
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "ServiceIO.hpp"
#include "XbotServiceInterface.hpp"

namespace xbot::serviceif {
class ServiceInterfaceBase : public xbot::serviceif::ServiceIOCallbacks,
                             public xbot::serviceif::ServiceDiscoveryCallbacks {
 public:
  ServiceInterfaceBase(uint16_t service_id, std::string type, uint32_t version, Context ctx);
  ~ServiceInterfaceBase() override;

  void Start();

  enum RPC_RESULT {
    RPC_OK = 0,
    RPC_ERROR,
    RPC_TIMEOUT,
    // The service answered RpcStatus::BUSY: it is still working on another call, or it has no free
    // block in its shared buffer pool. The call was refused for now and can be sent again.
    RPC_BUSY,
  };

  /**
   * Result of the most recent RPC of this interface. The generated Call...() methods only return
   * true or false; this tells a failed call that is worth repeating (RPC_BUSY) from one that is not.
   *
   * It is one value per interface: with several threads calling at the same time it may already
   * belong to another thread's call.
   */
  RPC_RESULT GetLastRpcResult() const {
    return last_rpc_result_;
  }

 protected:
  const uint16_t service_id_;
  // Type of the service (e.g. IMU Service)
  const std::string type_;
  // Version of the service (changes whenever the interface introduces breaking
  // changes)
  const uint32_t version_;

  bool StartTransaction(bool is_configuration = false);

  bool CommitTransaction();

  bool SendData(uint16_t target_id, const void *data, size_t size, bool is_configuration);

  /**
   * Sends an RPC_CALL packet, waits for response and returns the response in the provided buffer.
   * @param function_id ID of the RPC function to call
   * @param params Pointer to the parameters buffer
   * @param params_size Size of the parameters buffer
   * @param response_buffer Pointer to the buffer where the response will be stored
   * @param response_buffer_size Size of the response buffer (will be updated with actual response size)
   */
  RPC_RESULT SendRpc(uint8_t function_id, const uint8_t *params, size_t params_size, uint8_t* response_buffer, size_t *response_buffer_size, uint32_t timeout_ms = 1000);

  /**
   * Saturating subtraction for the generated frame budget helpers (Max<Function><Parameter>()): what
   * is left of budget after everything in used. A call whose parameters use up the whole packet
   * reports 0 instead of a wrapped (huge) budget.
   */
  static constexpr size_t RemainingBudget(size_t budget, size_t used) {
    return (used < budget) ? (budget - used) : 0;
  }
  template <typename... Rest>
  static constexpr size_t RemainingBudget(size_t budget, size_t used, Rest... rest) {
    return RemainingBudget(RemainingBudget(budget, used), rest...);
  }

 public:
  bool OnServiceDiscovered(uint16_t service_id) final;

  void OnRpcResponse(uint16_t service_id, uint16_t call_id, uint8_t status, const void *payload, size_t len) final;

  void OnServiceDisconnected(uint16_t service_id) override;

 private:
  void FillHeader();
  void MarkServiceDisconnected(uint16_t service_id);

  RPC_RESULT DoSendRpc(uint8_t function_id, const uint8_t *params, size_t params_size, uint8_t *response_buffer,
                       size_t *response_buffer_size, uint32_t timeout_ms);

  std::atomic<RPC_RESULT> last_rpc_result_{RPC_OK};

  // RPC synchronization state shared between generated Call* methods and OnRpcResponse.
  std::mutex rpc_mutex_{};
  std::condition_variable rpc_cv_{};
  uint16_t rpc_call_counter_{0};
  uint16_t pending_call_id_{0};
  bool rpc_call_active_{false};
  uint8_t rpc_response_status_{0};
  uint8_t *rpc_response_payload_{nullptr};
  size_t *rpc_response_payload_size_{nullptr};
  size_t rpc_received_size_{0};

  // Scratch space for the header.
  xbot::datatypes::XbotHeader header_{};
  std::vector<uint8_t> buffer_{};
  bool transaction_started_{false};
  bool is_configuration_transaction_{false};

  std::recursive_mutex state_mutex_{};

  std::atomic<bool> service_discovered_{false};
  bool io_callbacks_registered_{false};

  Context ctx{};
};
}  // namespace xbot::serviceif

#endif  // SERVICEINTERFACEBASE_HPP
