//
// Created by clemens on 7/14/24.
//
#include <ulog.h>
#include <xbot-service/ServiceIo.h>

#include <xbot/datatypes/XbotHeader.hpp>

namespace xbot::service {

ServiceIo::ServiceIo(uint32_t service_id) : service_id_(service_id), next_service_(nullptr) {
}

bool ServiceIo::ioInput(packet::PacketPtr packet) {
  if (!queue::queuePushItem(&packet_queue_, packet)) {
    ULOG_ARG_ERROR(&service_id_, "Error pushing packet into processing queue.");
    packet::freePacket(packet);
    return false;
  }
  return true;
}
}  // namespace xbot::service
