//
// Created by clemens on 4/29/24.
//

#ifndef SERVICEDISCOVERY_HPP
#define SERVICEDISCOVERY_HPP

#include <xbot-service-interface/data/ServiceInfo.hpp>

namespace xbot::serviceif {
class ServiceDiscoveryCallbacks {
 public:
  virtual ~ServiceDiscoveryCallbacks() = default;

  virtual bool OnServiceDiscovered(uint16_t service_id) = 0;

  virtual bool OnEndpointChanged(uint16_t service_id, uint32_t old_ip, uint16_t old_port, uint32_t new_ip,
                                 uint16_t new_port) = 0;
};

class ServiceDiscovery {
 public:
  virtual ~ServiceDiscovery() = default;

  virtual void RegisterCallbacks(ServiceDiscoveryCallbacks *callbacks) = 0;

  virtual void UnregisterCallbacks(ServiceDiscoveryCallbacks *callbacks) = 0;

  /**
   * Gets a copy of the ServiceInfo registered for this service_id.
   * @return a unique_ptr to a copy of the ServiceInfo. nullptr if none was
   * found.
   */
  virtual std::unique_ptr<ServiceInfo> GetServiceInfo(uint16_t service_id) = 0;
};
}  // namespace xbot::serviceif

#endif  // SERVICEDISCOVERY_HPP
