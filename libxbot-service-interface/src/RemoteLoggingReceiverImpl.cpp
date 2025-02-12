//
// Created by clemens on 07.02.25.
//

#include "RemoteLoggingReceiverImpl.hpp"

#include <xbot-service-interface/Socket.hpp>
#include <xbot/config.hpp>
#include <xbot/datatypes/XbotHeader.hpp>

#include "spdlog/spdlog.h"

namespace xbot::serviceif {
RemoteLoggingReceiverImpl *RemoteLoggingReceiverImpl::instance_ = nullptr;
std::mutex RemoteLoggingReceiverImpl::instance_mtx_{};

bool RemoteLoggingReceiverImpl::Start() {
  if (!rl_socket_.Start()) return false;

  if (!rl_socket_.JoinMulticast(config::remote_log_multicast_address)) return false;

  // Start the Thread
  {
    std::unique_lock lk{stopped_mtx_};
    stopped_ = false;
  }
  rl_thread_ = std::thread{std::bind(&RemoteLoggingReceiverImpl::Run, this)};
  return true;
}

void RemoteLoggingReceiverImpl::Run() {
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
    if (rl_socket_.ReceivePacket(sender_ip, sender_port, packet)) {
      // Check, if packet has at least enough space for our header
      if (packet.size() >= sizeof(datatypes::XbotHeader)) {
        const auto header = reinterpret_cast<datatypes::XbotHeader *>(packet.data());

        if (header->message_type != datatypes::MessageType::LOG) {
          spdlog::warn("Logging socket got non-logging message");
          continue;
        }

        // Validate reported length
        if (packet.size() == header->payload_size + sizeof(datatypes::XbotHeader)) {
          if (header->payload_size > 1) {
            std::string_view view{reinterpret_cast<const char *>(packet.data() + sizeof(datatypes::XbotHeader)),
                                  header->payload_size};
            switch (header->arg1) {
              case 1: spdlog::trace(">>> {}", view); break;
              case 2: spdlog::debug(">>> {}", view); break;
              case 3: spdlog::info(">>> {}", view); break;
              case 4: spdlog::warn(">>> {}", view); break;
              default: spdlog::error(">>> {}", view); break;
            }
          }
        }
      }
    }
  }
}

void RemoteLoggingReceiverImpl::SetMulticastIfAddress(std::string multicast_if_address) {
  GetInstance()->rl_socket_.SetMulticastIfAddress(multicast_if_address);
}

RemoteLoggingReceiverImpl *RemoteLoggingReceiverImpl::GetInstance() {
  std::unique_lock lk{instance_mtx_};
  if (instance_ == nullptr) {
    instance_ = new RemoteLoggingReceiverImpl();
  }
  return instance_;
}

bool RemoteLoggingReceiverImpl::Stop() {
  spdlog::info("Shutting down RemoteLoggingReceiver");
  {
    std::unique_lock lk{stopped_mtx_};
    stopped_ = true;
  }
  rl_thread_.join();
  spdlog::info("RemoteLoggingReceiver Stopped.");
  return true;
}
}  // namespace xbot::serviceif
