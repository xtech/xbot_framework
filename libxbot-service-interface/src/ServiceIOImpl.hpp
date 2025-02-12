//
// Created by clemens on 7/21/24.
//

#ifndef XBOT_FRAMEWORK_SERVICEIOIMPL_HPP
#define XBOT_FRAMEWORK_SERVICEIOIMPL_HPP

#include <chrono>
#include <xbot-service-interface/ServiceDiscovery.hpp>
#include <xbot-service-interface/ServiceIO.hpp>

namespace xbot::serviceif {
// Keep track of the state of each service (claimed or not, timeout)
struct ServiceState {
  // track, if we have claimed the service successfully.
  // If the service is claimed it will send its outputs to this interface.
  bool claimed_successfully_{false};

  // track when we sent the last claim, so that we don't spam the service
  std::chrono::time_point<std::chrono::steady_clock> last_claim_sent_{std::chrono::seconds(0)};
  std::chrono::time_point<std::chrono::steady_clock> last_heartbeat_received_{std::chrono::seconds(0)};
};

/**
 * ServiceIO subscribes to ServiceDiscovery and claims all services anyone
 * is interested in. It keeps track of the timeouts and redirects the actual
 * data to the actual subscribers.
 */
class ServiceIOImpl : public ServiceIO, public ServiceDiscoveryCallbacks {
 public:
  bool OnServiceDiscovered(uint16_t service_id) override;

  bool OnEndpointChanged(uint16_t service_id, uint32_t old_ip, uint16_t old_port, uint32_t new_ip,
                         uint16_t new_port) override;

  static void SetBindAddress(std::string bind_address);

  static ServiceIOImpl *GetInstance();

  bool Start();

  bool Stop();

  // Deleted copy constructor
  ServiceIOImpl(ServiceIOImpl &other) = delete;

  // Deleted assignment operator
  void operator=(const ServiceIO &) = delete;

  /**
   * Register callbacks for a specific uid
   * @param service_id the service ID to listen to
   * @param callbacks pointer to the callbacks
   */
  void RegisterCallbacks(uint16_t service_id, ServiceIOCallbacks *callbacks) override;

  /**
   * Unregister callbacks for all ids
   * @param callbacks callback pointer
   */
  void UnregisterCallbacks(ServiceIOCallbacks *callbacks) override;

  /**
   * Send data to a given service target
   */
  bool SendData(uint16_t service_id, const std::vector<uint8_t> &data) override;

  explicit ServiceIOImpl(ServiceDiscoveryImpl *serviceDiscovery);

  ~ServiceIOImpl() override = default;

  bool OK() final;

 private:
  ServiceDiscoveryImpl *const service_discovery;

  void RunIo();

  void ClaimService(uint16_t service_id);

  bool TransmitPacket(uint32_t ip, uint16_t port, const std::vector<uint8_t> &data);

  void HandleClaimMessage(datatypes::XbotHeader *header, const uint8_t *payload, size_t payload_len);

  void HandleDataMessage(datatypes::XbotHeader *header, const uint8_t *payload, size_t payload_len);

  void HandleDataTransaction(datatypes::XbotHeader *header, const uint8_t *payload, size_t payload_len);

  void HandleHeartbeatMessage(datatypes::XbotHeader *header, const uint8_t *payload, size_t payload_len);

  void HandleConfigurationRequest(datatypes::XbotHeader *header, const uint8_t *payload, size_t payload_len);
};
}  // namespace xbot::serviceif
#endif  // XBOT_FRAMEWORK_SERVICEIOIMPL_HPP
