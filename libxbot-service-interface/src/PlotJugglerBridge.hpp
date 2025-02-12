//
// Created by clemens on 7/16/24.
//

#ifndef PLOTJUGGLERBRIDGE_HPP
#define PLOTJUGGLERBRIDGE_HPP

#include <xbot-service-interface/ServiceDiscovery.hpp>
#include <xbot-service-interface/ServiceIO.hpp>
#include <xbot-service-interface/Socket.hpp>
#include <xbot-service-interface/XbotServiceInterface.hpp>

using namespace xbot;

/**
 * PlotJugglerBridge is used to bridge between xbot services and PlotJuggler for
 * easy debugging.
 *
 * It will listen for any nodes detected and redirect the data.
 *
 * Output data is UDP as JSON.
 *
 * The JSON has the following format:
 * {
 *  "service_id": {
 *    "input_name": { "the": "actual data" }
 *  }
 * }
 */
class PlotJugglerBridge : public serviceif::ServiceDiscoveryCallbacks, public serviceif::ServiceIOCallbacks {
 public:
  explicit PlotJugglerBridge(xbot::serviceif::Context ctx);

  ~PlotJugglerBridge() override;

  bool Start();

  bool OnServiceDiscovered(uint16_t service_id) override;

  bool OnEndpointChanged(uint16_t service_id, uint32_t old_ip, uint16_t old_port, uint32_t new_ip,
                         uint16_t new_port) override;

  void OnServiceConnected(uint16_t service_id) override;

  void OnData(uint16_t service_id, uint64_t timestamp, uint16_t target_id, const void *payload, size_t buflen) override;

  void OnServiceDisconnected(uint16_t service_id) override;

 private:
  std::mutex state_mutex_{};
  // Stores a map of <uid, output> pairs to quickly find the ServiceIOInfo
  std::map<std::pair<uint16_t, uint16_t>, ServiceIOInfo> topic_map_{};
  serviceif::Socket socket_{"0.0.0.0"};

  const serviceif::Context ctx;
};
#endif  // PLOTJUGGLERBRIDGE_HPP
