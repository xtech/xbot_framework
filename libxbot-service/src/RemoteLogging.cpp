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
char buffer[xbot::config::max_log_length];
ulog_level_t log_level = ULOG_DEBUG_LEVEL;

void remote_logger(ulog_level_t severity, char* msg, const void* args) {
  (void)args;

  if (severity < log_level) {
    return;
  }

  Lock lk(&logging_mutex);

  // Packet Header
  log_message_header.protocol_version = 1;
  log_message_header.message_type = MessageType::LOG;
  log_message_header.flags = 0;

  log_message_header.arg1 = (severity - ULOG_TRACE_LEVEL + 1);
  log_message_header.arg2 = 0;
  log_message_header.sequence_no = log_sequence_no++;
  log_message_header.timestamp = 0;

  size_t msg_len;
  void* msg_ptr;
  if (args != nullptr) {
    msg_len = snprintf(buffer, sizeof(buffer), "[ID=%i] %s", *static_cast<const uint16_t*>(args), msg);
    msg_ptr = buffer;
  } else {
    msg_len = strnlen(msg, xbot::config::max_log_length);
    msg_ptr = msg;
  }

  if (msg_len > 0 && msg_len < xbot::config::max_log_length) {
    log_message_header.payload_size = msg_len;
    packet::PacketPtr log_packet = packet::allocatePacket();
    packetAppendData(log_packet, &log_message_header, sizeof(log_message_header));
    packetAppendData(log_packet, msg_ptr, log_message_header.payload_size);
    sock::transmitPacket(&logging_socket, log_packet, xbot::config::remote_log_multicast_address,
                         xbot::config::multicast_port);
  }
}

bool xbot::service::startRemoteLogging(ulog_level_t level) {
  if (!mutex::initialize(&logging_mutex)) {
    return false;
  }

  Lock lk(&logging_mutex);
  log_level = level;
  if (!sock::initialize(&logging_socket, false)) {
    ULOG_ERROR("Error setting up remote logging: Error creating socket");
    return false;
  }

  ULOG_SUBSCRIBE(remote_logger, ULOG_DEBUG_LEVEL);
  return true;
}
