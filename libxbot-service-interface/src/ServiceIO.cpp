//
// Created by clemens on 4/30/24.
//

#include <spdlog/spdlog.h>

#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <thread>
#include <xbot-service-interface/Socket.hpp>
#include <xbot/datatypes/ClaimPayload.hpp>

#include "ServiceDiscoveryImpl.hpp"
#include "ServiceIOImpl.hpp"
#include "xbot-service-interface/endpoint_utils.hpp"

using namespace xbot::serviceif;

/**
 * Maps service_id to state.
 */
std::map<uint16_t, std::unique_ptr<ServiceState> > endpoint_map_{};

// Protects inverse_endpoint_map_, endpoint_map_ and instance_
std::recursive_mutex state_mutex_{};
ServiceIOImpl *instance_ = nullptr;

std::thread io_thread_{};
Socket io_socket_{"0.0.0.0"};
std::mutex stopped_mtx_{};
bool stopped_{false};
// track when we last checked for claims and timeouts
std::chrono::time_point<std::chrono::steady_clock> last_check_{std::chrono::seconds(0)};

// keep a list of callbacks for each service
std::map<uint16_t, std::vector<ServiceIOCallbacks *> > registered_callbacks_{};

bool ServiceIOImpl::OnServiceDiscovered(uint16_t service_id) {
  std::unique_lock lk{state_mutex_};

  uint32_t service_ip = 0;
  uint16_t service_port = 0;

  if (service_discovery->GetEndpoint(service_id, service_ip, service_port) && service_ip != 0 && service_port != 0) {
    // Got valid endpoint, create a state
    if (endpoint_map_.contains(service_id)) {
      spdlog::warn(
          "[ID={}] Service state already exists, overwriting with new state. "
          "This might have unforseen consequences.",
          service_id);
      endpoint_map_.erase(service_id);
    }
    std::unique_ptr<ServiceState> state = std::make_unique<ServiceState>();
    endpoint_map_.emplace(service_id, std::move(state));
  }

  return true;
}

bool ServiceIOImpl::OnEndpointChanged(uint16_t service_id, uint32_t old_ip, uint16_t old_port, uint32_t new_ip,
                                      uint16_t new_port) {
  // We don't actually care about endpoint changes.
  return true;
}

void ServiceIOImpl::SetBindAddress(std::string bind_address) {
  io_socket_.SetBindAddress(bind_address);
}

ServiceIOImpl *ServiceIOImpl::GetInstance() {
  std::unique_lock lk{state_mutex_};
  if (instance_ == nullptr) {
    instance_ = new ServiceIOImpl(ServiceDiscoveryImpl::GetInstance());
  }
  return instance_;
}

bool ServiceIOImpl::Start() {
  {
    std::unique_lock lk{stopped_mtx_};
    stopped_ = false;
  }
  io_thread_ = std::thread{&ServiceIOImpl::RunIo, this};

  return true;
}

void ServiceIOImpl::RegisterCallbacks(uint16_t service_id, ServiceIOCallbacks *callbacks) {
  std::unique_lock lk{state_mutex_};
  // Square bracket notation is correct, we create an empty vector, if non-existent
  auto &vector = registered_callbacks_[service_id];

  // Check, if callbacks already registered
  if (std::find(vector.begin(), vector.end(), callbacks) != vector.end()) {
    return;
  }

  // add the callbacks
  vector.push_back(callbacks);
}

void ServiceIOImpl::UnregisterCallbacks(ServiceIOCallbacks *callbacks) {
  std::unique_lock lk{state_mutex_};
  for (auto [service_id, callback_list] : registered_callbacks_) {
    for (auto cb_it = callback_list.begin(); cb_it != callback_list.end();) {
      if (*cb_it == callbacks) {
        // Erase
        cb_it = callback_list.erase(cb_it);
      } else {
        // Skip
        ++cb_it;
      }
    }
  }
}

bool ServiceIOImpl::SendData(uint16_t service_id, const std::vector<uint8_t> &data) {
  uint32_t ip = 0;
  uint16_t port = 0;
  if (!service_discovery->GetEndpoint(service_id, ip, port)) {
    spdlog::warn("[ID={}] no endpoint for service", service_id);
    return false;
  }

  return TransmitPacket(ip, port, data);
}

void ServiceIOImpl::RunIo() {
  // Set timeout so that we can detect missing heartbeat
  io_socket_.SetReceiveTimeoutMicros(config::default_heartbeat_micros);
  io_socket_.Start();
  uint32_t sender_ip;
  uint16_t sender_port;
  std::vector<uint8_t> packet{};
  // While not stopped
  while (true) {
    {
      std::unique_lock lk{stopped_mtx_};
      if (stopped_) break;
    }
    if (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - last_check_) >
        std::chrono::seconds(1)) {
      last_check_ = std::chrono::steady_clock::now();
      spdlog::debug("running checks");
      std::unique_lock lk{state_mutex_};
      // Claim all unclaimed services and check for timeouts.
      for (auto it = endpoint_map_.begin(); it != endpoint_map_.end(); /* no increment */) {
        const uint16_t service_id = it->first;
        if (!it->second->claimed_successfully_) {
          ClaimService(service_id);
          ++it;
        } else {
          // Check for timeout
          if (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                                    it->second->last_heartbeat_received_) >
              std::chrono::microseconds(config::default_heartbeat_micros + config::heartbeat_jitter)) {
            spdlog::warn("[ID={}] Service timed out, removing service.", service_id);

            // Drop service from discovery, so that it will get
            // rediscovered later
            service_discovery->DropService(service_id);

            // Notify callbacks for that service
            if (const auto cb_it = registered_callbacks_.find(service_id); cb_it != registered_callbacks_.end()) {
              for (const auto &cb : cb_it->second) {
                cb->OnServiceDisconnected(service_id);
              }
            }

            it = endpoint_map_.erase(it);
          } else {
            // No timeout, go to next
            ++it;
          }
        }
      }
    }

    if (io_socket_.ReceivePacket(sender_ip, sender_port, packet)) {
      if (packet.size() >= sizeof(datatypes::XbotHeader)) {
        const auto header = reinterpret_cast<datatypes::XbotHeader *>(packet.data());
        if (header->payload_size != packet.size() - sizeof(datatypes::XbotHeader)) {
          spdlog::error("Got packet with invalid size");
          continue;
        }

        const uint8_t *const payload_buffer = packet.data() + sizeof(datatypes::XbotHeader);

        switch (header->message_type) {
          case datatypes::MessageType::CLAIM: HandleClaimMessage(header, payload_buffer, header->payload_size); break;
          case datatypes::MessageType::DATA: HandleDataMessage(header, payload_buffer, header->payload_size); break;
          case datatypes::MessageType::CONFIGURATION_REQUEST:
            HandleConfigurationRequest(header, payload_buffer, header->payload_size);
            break;
          case datatypes::MessageType::HEARTBEAT:
            HandleHeartbeatMessage(header, payload_buffer, header->payload_size);
            break;
          case datatypes::MessageType::TRANSACTION:
            if (header->arg1 == 0) {
              HandleDataTransaction(header, payload_buffer, header->payload_size);
            } else {
              spdlog::warn("Got transaction with unknown type");
            }
            break;
          default: spdlog::warn("Got message of unknown type"); break;
        }
      }
    }
  }
}

void ServiceIOImpl::ClaimService(uint16_t service_id) {
  std::unique_lock lk{state_mutex_};
  if (!endpoint_map_.contains(service_id)) {
    // Cannot try to claim a service which was not even discovered
    spdlog::error("[ID={}] Tried to claim a service which was not in the endpoint map", service_id);
    return;
  }
  const auto &state = endpoint_map_.at(service_id);

  // Check, if we recently sent the claim. If not, try again
  auto now = std::chrono::steady_clock::now();

  auto diff = std::chrono::duration_cast<std::chrono::microseconds>(now - state->last_claim_sent_);
  if (diff < std::chrono::microseconds(1000)) {
    return;
  }

  // Set it here, in case of error we also don't want to retry too often
  state->last_claim_sent_ = now;
  state->claimed_successfully_ = false;

  std::string my_ip{};
  uint16_t my_port;
  if (!io_socket_.GetEndpoint(my_ip, my_port) || my_ip == "0.0.0.0" || my_ip.empty() || my_port == 0) {
    spdlog::warn("[ID={}] Could not claim service, interface socket is not bound yet", service_id);
    return;
  }

  std::vector<uint8_t> packet{};
  packet.resize(sizeof(datatypes::XbotHeader) + sizeof(datatypes::ClaimPayload));
  auto header = reinterpret_cast<datatypes::XbotHeader *>(packet.data());
  header->message_type = datatypes::MessageType::CLAIM;
  header->service_id = service_id;
  header->protocol_version = 1;
  header->arg1 = 0;
  header->arg2 = 0;
  header->sequence_no = 0;
  header->flags = 0;
  header->timestamp =
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  header->payload_size = sizeof(datatypes::ClaimPayload);
  auto payload_ptr = reinterpret_cast<datatypes::ClaimPayload *>(packet.data() + sizeof(datatypes::XbotHeader));
  payload_ptr->target_ip = IpStringToInt(my_ip);
  payload_ptr->target_port = my_port;
  payload_ptr->heartbeat_micros = config::default_heartbeat_micros;
  spdlog::info("[ID={}] Sending Service Claim", service_id);
  SendData(service_id, packet);
}

bool ServiceIOImpl::TransmitPacket(uint32_t ip, uint16_t port, const std::vector<uint8_t> &data) {
  if (ip == 0 || port == 0) {
    return false;
  }
  return io_socket_.TransmitPacket(ip, port, data);
}

void ServiceIOImpl::HandleClaimMessage(xbot::datatypes::XbotHeader *header, const uint8_t *payload,
                                       size_t payload_len) {
  uint16_t service_id = header->service_id;
  if (header->arg1 != 1) {
    // Not actually an ack
    spdlog::warn("[ID={}] received claim ack without arg1==1", service_id);
    return;
  }

  std::unique_lock lk{state_mutex_};
  if (!endpoint_map_.contains(service_id)) {
    spdlog::warn("[ID={}] received claim ack from an unknown service", service_id);
    return;
  }
  const auto &ptr = endpoint_map_.at(service_id);
  // Also count the ack as heartbeat in order to not instantly timeout
  ptr->last_heartbeat_received_ = std::chrono::steady_clock::now();

  if (ptr->claimed_successfully_) {
    spdlog::warn("[ID={}] claim ack from already claimed service", service_id);
    return;
  }
  ptr->claimed_successfully_ = true;
  spdlog::info("[ID={}] Successfully claimed service", service_id);

  // Notify callbacks for that service
  if (const auto it = registered_callbacks_.find(service_id); it != registered_callbacks_.end()) {
    for (const auto &cb : it->second) {
      cb->OnServiceConnected(service_id);
    }
  }
}

void ServiceIOImpl::HandleDataMessage(xbot::datatypes::XbotHeader *header, const uint8_t *payload, size_t payload_len) {
  uint16_t service_id = header->service_id;
  {
    std::unique_lock lk{state_mutex_};
    if (!endpoint_map_.contains(service_id)) {
      spdlog::debug("[ID={}] Got data from an unknown service, dropping it", service_id);
      return;
    }
    if (!endpoint_map_.at(service_id)->claimed_successfully_) {
      spdlog::debug("[ID={}] Got data from an unclaimed service, dropping it.", service_id);
      return;
    }
  }
  const auto &ptr = endpoint_map_.at(service_id);

  // Notify callbacks for that service
  if (const auto it = registered_callbacks_.find(service_id); it != registered_callbacks_.end()) {
    for (const auto &cb : it->second) {
      cb->OnData(service_id, header->timestamp, header->arg2, payload, header->payload_size);
    }
  }
}

void ServiceIOImpl::HandleDataTransaction(xbot::datatypes::XbotHeader *header, const uint8_t *payload,
                                          size_t payload_len) {
  uint16_t service_id = header->service_id;
  std::unique_lock lk{state_mutex_};
  if (!endpoint_map_.contains(service_id)) {
    // This happens if we restart the interface and an unknown service sends
    // us data.
    spdlog::debug("[ID={}] Got data from an unknown service", service_id);
    return;
  }
  if (!endpoint_map_.at(service_id)->claimed_successfully_) {
    // This happens if we restart the interface and a previously claimed
    // service is still sending data.
    spdlog::debug("[ID={}] Got data from an unclaimed service, dropping it.", service_id);
    return;
  }

  // Notify callbacks for that service
  if (const auto it = registered_callbacks_.find(service_id); it != registered_callbacks_.end()) {
    for (const auto &cb : it->second) {
      cb->OnTransactionStart(header->timestamp);
      // Go through all data packets in the transaction
      size_t processed_len = 0;
      while (processed_len + sizeof(datatypes::DataDescriptor) <= header->payload_size) {
        // we have at least enough data for the next descriptor, read it
        const auto descriptor = reinterpret_cast<const datatypes::DataDescriptor *>(payload + processed_len);
        size_t data_size = descriptor->payload_size;
        if (processed_len + sizeof(datatypes::DataDescriptor) + data_size <= header->payload_size) {
          // we can safely read the data
          cb->OnData(service_id, header->timestamp, descriptor->target_id,
                     payload + processed_len + sizeof(datatypes::DataDescriptor), data_size);
        } else {
          spdlog::error(
              "Error parsing transaction, header payload size does not "
              "match transaction size!");
          break;
        }
        processed_len += data_size + sizeof(datatypes::DataDescriptor);
      }

      if (processed_len != header->payload_size) {
        spdlog::warn("Transaction size mismatch!");
      }

      cb->OnTransactionEnd();
    }
  }
}

void ServiceIOImpl::HandleHeartbeatMessage(xbot::datatypes::XbotHeader *header, const uint8_t *payload,
                                           size_t payload_len) {
  uint16_t service_id = header->service_id;

  std::unique_lock lk{state_mutex_};
  if (!endpoint_map_.contains(service_id)) {
    spdlog::warn("[ID={}] Received heartbeat from an unknown service", service_id);
    return;
  }
  endpoint_map_.at(service_id)->last_heartbeat_received_ = std::chrono::steady_clock::now();
}

void ServiceIOImpl::HandleConfigurationRequest(xbot::datatypes::XbotHeader *header, const uint8_t *payload,
                                               size_t payload_len) {
  uint16_t service_id = header->service_id;

  std::unique_lock lk{state_mutex_};
  if (!endpoint_map_.contains(service_id)) {
    spdlog::debug("[ID={}] Got config request from an unknown service, dropping it", service_id);
    return;
  }
  if (!endpoint_map_.at(service_id)->claimed_successfully_) {
    spdlog::debug("[ID={}] Got config request from an unclaimed service, dropping it.", service_id);
    return;
  }

  const auto &ptr = endpoint_map_.at(service_id);

  // Notify callbacks for that service
  bool configuration_handled = false;
  if (const auto it = registered_callbacks_.find(service_id); it != registered_callbacks_.end()) {
    for (const auto &cb : it->second) {
      if (cb->OnConfigurationRequested(service_id)) {
        configuration_handled = true;
        break;
      }
    }
  }
  if (!configuration_handled) {
    spdlog::warn(
        "[ID={}] service requires configuration, but no handler provided any configuration. "
        "The service won't start.",
        service_id);
  }
}

ServiceIOImpl::ServiceIOImpl(ServiceDiscoveryImpl *serviceDiscovery) : service_discovery(serviceDiscovery) {
}

bool ServiceIOImpl::OK() {
  std::unique_lock lk{stopped_mtx_};
  return !stopped_;
}

bool ServiceIOImpl::Stop() {
  spdlog::info("Shutting down ServiceIO");
  {
    std::unique_lock lk{stopped_mtx_};
    stopped_ = true;
  }
  io_thread_.join();
  spdlog::info("ServiceIO Stopped.");
  return true;
}
