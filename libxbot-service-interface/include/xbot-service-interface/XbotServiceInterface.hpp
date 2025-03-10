//
// Created by clemens on 7/22/24.
//

#ifndef XBOT_FRAMEWORK_XBOTSERVICEINTERFACE_HPP
#define XBOT_FRAMEWORK_XBOTSERVICEINTERFACE_HPP

#include <xbot-service-interface/ServiceDiscovery.hpp>
#include <xbot-service-interface/ServiceIO.hpp>

namespace xbot::serviceif {
struct Context {
  ServiceIO *io = nullptr;
  ServiceDiscovery *serviceDiscovery = nullptr;
  void *ctx = nullptr;
};

/**
 * Call this method to start xbot_framework.
 *
 * @param register_signal_handlers true to handle signals (CTRL+C), false to
 * manually stop using Stop()
 * @return The context
 */
Context Start(bool register_signal_handlers = true, std::string bind_ip = "0.0.0.0", bool start_rest_api = false);
void Stop();
}  // namespace xbot::serviceif

#endif  // XBOT_FRAMEWORK_XBOTSERVICEINTERFACE_HPP
