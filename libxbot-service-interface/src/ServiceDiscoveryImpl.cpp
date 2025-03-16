//
// Created by clemens on 4/29/24.
//

#include "ServiceDiscoveryImpl.hpp"

#include <mutex>
#include <nlohmann/json.hpp>
#include <xbot-service-interface/Socket.hpp>
#include <xbot-service-interface/data/ServiceInfo.hpp>
#include <xbot/config.hpp>
#include <xbot/datatypes/XbotHeader.hpp>

#include "spdlog/spdlog.h"

namespace xbot::serviceif {
static void Run();

std::recursive_mutex sd_mutex_{};
// Keep track of discovered servies and their endpoints.
std::map<uint16_t, ServiceInfo> discovered_services_{};

std::mutex stopped_mtx_{};
bool stopped_{false};

std::thread sd_thread_{};
Socket sd_socket_{config::sd_multicast_address, config::multicast_port};
std::vector<ServiceDiscoveryCallbacks *> registered_callbacks_{};

ServiceDiscoveryImpl *instance_ = nullptr;

bool ServiceDiscoveryImpl::GetEndpoint(uint16_t service_id, uint32_t &ip, uint16_t &port) {
  std::unique_lock lk(sd_mutex_);
  if (discovered_services_.contains(service_id)) {
    auto &service_info = discovered_services_.at(service_id);
    ip = service_info.ip;
    port = service_info.port;
    return true;
  }
  ip = 0;
  port = 0;
  return false;
}

bool ServiceDiscoveryImpl::Start() {
  if (!sd_socket_.Start()) return false;

  if (!sd_socket_.JoinMulticast(config::sd_multicast_address)) return false;

  // Start the ServiceDiscovery Thread
  {
    std::unique_lock lk{stopped_mtx_};
    stopped_ = false;
  }
  sd_thread_ = std::thread{Run};
  return true;
}

void ServiceDiscoveryImpl::RegisterCallbacks(ServiceDiscoveryCallbacks *callbacks) {
  if (callbacks == nullptr) {
    return;
  }
  std::unique_lock lk(sd_mutex_);
  const auto &it = std::find(registered_callbacks_.begin(), registered_callbacks_.end(), callbacks);
  if (it == registered_callbacks_.end()) {
    registered_callbacks_.push_back(callbacks);

    // Also call OnServiceDiscovered for all services we already know, so that the interface can register for data if
    // needed
    for (const auto &service : discovered_services_) {
      callbacks->OnServiceDiscovered(service.second.service_id_);
    }
  }
}

void ServiceDiscoveryImpl::UnregisterCallbacks(ServiceDiscoveryCallbacks *callbacks) {
  if (callbacks == nullptr) {
    return;
  }
  std::unique_lock lk(sd_mutex_);

  while (true) {
    const auto &it = std::find(registered_callbacks_.begin(), registered_callbacks_.end(), callbacks);
    if (it != registered_callbacks_.end()) {
      registered_callbacks_.erase(it);
    } else {
      break;
    }
  }
}

std::unique_ptr<ServiceInfo> ServiceDiscoveryImpl::GetServiceInfo(uint16_t service_id) {
  std::unique_lock lk(sd_mutex_);
  if (!discovered_services_.contains(service_id)) {
    return nullptr;
  }

  return std::make_unique<ServiceInfo>(discovered_services_.at(service_id));
}

std::unique_ptr<std::map<uint16_t, ServiceInfo> > ServiceDiscoveryImpl::GetAllServices() {
  std::unique_lock lk(sd_mutex_);
  return std::make_unique<std::map<uint16_t, ServiceInfo> >(discovered_services_);
}

bool ServiceDiscoveryImpl::DropService(uint16_t service_id) {
  std::unique_lock lk(sd_mutex_);
  return discovered_services_.erase(service_id) > 0;
}

ServiceDiscoveryImpl *ServiceDiscoveryImpl::GetInstance() {
  std::unique_lock lk{sd_mutex_};
  if (instance_ == nullptr) {
    instance_ = new ServiceDiscoveryImpl();
  }
  return instance_;
}

void ServiceDiscoveryImpl::SetMulticastIfAddress(std::string multicast_if_address) {
  sd_socket_.SetMulticastIfAddress(multicast_if_address);
}

bool ServiceDiscoveryImpl::Stop() {
  spdlog::info("Shutting down ServiceDiscovery");
  {
    std::unique_lock lk{stopped_mtx_};
    stopped_ = true;
  }
  sd_thread_.join();
  spdlog::info("ServiceDiscovery Stopped.");
  return true;
}

void Run() {
  std::vector<uint8_t> packet{};
  uint32_t sender_ip;
  uint16_t sender_port;
  // While not stopped
  while (true) {
    {
      std::unique_lock lk{stopped_mtx_};
      if (stopped_) break;
    }

    // Try receive a packet, this will return false on timeout.
    if (sd_socket_.ReceivePacket(sender_ip, sender_port, packet)) {
      // Check, if packet has at least enough space for our header
      if (packet.size() >= sizeof(datatypes::XbotHeader)) {
        const auto header = reinterpret_cast<datatypes::XbotHeader *>(packet.data());

        if (header->message_type != datatypes::MessageType::SERVICE_ADVERTISEMENT) {
          spdlog::warn("Service Discovery socket got non-service discovery message");
          continue;
        }

        // Validate reported length
        if (packet.size() == header->payload_size + sizeof(datatypes::XbotHeader)) {
          try {
            const auto json = nlohmann::json::from_cbor(packet.begin() + sizeof(datatypes::XbotHeader), packet.end());

            // Build the ServiceInfo object from the received data.
            ServiceInfo info = json;

            // Check for valid endpoint, if none is given the service is not
            // reachable, so we drop it
            if (info.ip == 0 || info.port == 0) {
              spdlog::warn(
                  "Service registered with invalid endpoint. Ignoring. (ID: "
                  "{}, endpoint: {})",
                  info.service_id_, EndpointIntToString(info.ip, info.port));
              continue;
            }

            // Scope for locking the discovered_services_ map
            {
              std::unique_lock lk(sd_mutex_);
              if (discovered_services_.contains(info.service_id_)) {
                // Check, if service endpoint was updated
                // (every thing else is constant) and update
                if (auto &old_service_info = discovered_services_.at(info.service_id_);
                    old_service_info.ip != info.ip || old_service_info.port != info.port) {
                  spdlog::info("Endpoint updated (ID: {}, new endpoint: {})", info.service_id_,
                               EndpointIntToString(info.ip, info.port));
                  // Backup the old infos, so that we can pass them to the
                  // callback
                  const auto old_ip = old_service_info.ip;
                  const auto old_port = old_service_info.port;

                  // Update the entry
                  old_service_info.ip = info.ip;
                  old_service_info.port = info.port;

                  // Notify callbacks
                  for (const auto &callback : registered_callbacks_) {
                    callback->OnEndpointChanged(info.service_id_, old_ip, old_port, info.ip, info.port);
                  }
                }
              } else {
                spdlog::info("Found new service (Type: {}, ID: {}, endpoint: {})", info.description.type,
                             info.service_id_, EndpointIntToString(info.ip, info.port));
                discovered_services_.emplace(info.service_id_, info);
                // Notify callbacks
                for (const auto &callback : registered_callbacks_) {
                  callback->OnServiceDiscovered(info.service_id_);
                }
              }
            }
          } catch (std::exception &e) {
            spdlog::error("Got exception during service discovery: {}", e.what());
          }
        }
      }
    }
  }
}
}  // namespace xbot::serviceif
