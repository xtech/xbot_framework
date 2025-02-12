//
// Created by clemens on 4/30/24.
//

#ifndef SERVICEINTERFACEFACTORY_HPP
#define SERVICEINTERFACEFACTORY_HPP

#include <string>
#include <xbot-service-interface/ServiceDiscovery.hpp>
#include <xbot/datatypes/XbotHeader.hpp>

namespace xbot::serviceif {
class ServiceIOCallbacks {
 public:
  virtual ~ServiceIOCallbacks() = default;

  /**
   * Called whenever a service is connected.
   * Connected means that it was discovered and claimed successfully.
   */
  virtual void OnServiceConnected(uint16_t service_id){};

  /**
   * Called whenever a new transaction starts
   */
  virtual void OnTransactionStart(uint64_t timestamp){};

  /**
   * Called whenever a transaction was finished
   */
  virtual void OnTransactionEnd(){};

  /**
   * Called whenever a packet is received from the specified service.
   * @param service_id service id
   * @param timestamp timestamp
   * @param target_id ID of the io target
   * @param payload the raw payload
   * @param buflen size of the payload buffer
   */
  virtual void OnData(uint16_t service_id, uint64_t timestamp, uint16_t target_id, const void *payload,
                      size_t buflen) = 0;

  /**
   * Called whenever a service needs configuration. Whenever this is called,
   * send a configuration transaction to the requesting service
   * @param service_id service id
   */
  virtual bool OnConfigurationRequested(uint16_t service_id) {
    return false;
  };

  /**
   * Called whenever a service is disconnected.
   * This could be due to timeout. After this OnData won't be called anymore
   * with the uid. Note that OnServiceConnected might be called on reconnection.
   * Then OnData will be called again as expected.
   * @param service_id the service's id
   */
  virtual void OnServiceDisconnected(uint16_t service_id){};
};

/**
 * ServiceIO subscribes to ServiceDiscovery and claims all services anyone
 * is interested in. It keeps track of the timeouts and redirects the actual
 * data to the actual subscribers.
 */
class ServiceIO {
 public:
  /**
   * Register callbacks for a specific uid
   * @param service_id the service ID to listen for
   * @param callbacks pointer to the callbacks
   */
  virtual void RegisterCallbacks(uint16_t service_id, ServiceIOCallbacks *callbacks) = 0;

  /**
   * Unregister callbacks for all uids
   * @param callbacks callback pointer
   */
  virtual void UnregisterCallbacks(ServiceIOCallbacks *callbacks) = 0;

  /**
   * Send data to a given service target
   */
  virtual bool SendData(uint16_t service_id, const std::vector<uint8_t> &data) = 0;

  /**
   * Call this to check if IO is still running.
   * On shutdown this will return false, stop your interface then
   */
  virtual bool OK() = 0;
};
}  // namespace xbot::serviceif

#endif  // SERVICEINTERFACEFACTORY_HPP
