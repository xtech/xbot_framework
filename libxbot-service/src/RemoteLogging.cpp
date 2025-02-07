//
// Created by clemens on 3/25/24.
//

#include <ulog.h>

#include <cstdio>
#include <cstring>
#include <xbot-service/Lock.hpp>
#include <xbot-service/RemoteLogging.hpp>
#include <xbot-service/portable/socket.hpp>
#include <xbot/config.hpp>

#include "xbot/datatypes/LogPayload.hpp"
#include "xbot/datatypes/XbotHeader.hpp"

using namespace xbot::service;
using namespace xbot::datatypes;

XBOT_MUTEX_TYPEDEF logging_mutex{};
XBOT_SOCKET_TYPEDEF logging_socket{};
XbotHeader log_message_header{};

uint16_t log_sequence_no = 0;

void remote_logger(ulog_level_t severity, char* msg, const void* args) {
  (void)args;
  Lock lk(&logging_mutex);

  // Packet Header
  log_message_header.protocol_version = 1;
  log_message_header.message_type = MessageType::LOG;
  log_message_header.flags = 0;

  log_message_header.arg1 = (severity - ULOG_TRACE_LEVEL + 1);
  log_message_header.arg2 = 0;
  log_message_header.sequence_no = log_sequence_no++;
  log_message_header.timestamp = 0;

  size_t msg_len = strnlen(msg, xbot::config::max_log_length);

  if (msg_len < xbot::config::max_log_length) {
    log_message_header.payload_size = msg_len;
    packet::PacketPtr log_packet = packet::allocatePacket();
    packetAppendData(log_packet, &log_message_header, sizeof(log_message_header));
    packetAppendData(log_packet, msg, log_message_header.payload_size);
    sock::transmitPacket(&logging_socket, log_packet, xbot::config::remote_log_multicast_address,
                         xbot::config::multicast_port);
  }
}

bool xbot::service::startRemoteLogging() {
  if (!mutex::initialize(&logging_mutex)) {
    return false;
  }

  Lock lk(&logging_mutex);
  if (!sock::initialize(&logging_socket, false)) {
    ULOG_ERROR("Error setting up remote logging: Error creating socket");
    return false;
  }

  ULOG_SUBSCRIBE(remote_logger, ULOG_DEBUG_LEVEL);
  return true;
}
