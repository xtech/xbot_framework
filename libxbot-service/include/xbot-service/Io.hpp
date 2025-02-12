//
// Created by clemens on 7/14/24.
//

#ifndef IO_H
#define IO_H
#include <xbot-service/ServiceIo.h>
namespace xbot::service {
class Io {
 public:
  // Register a new service for IO.
  // This service will then be able to receive
  static bool registerServiceIo(ServiceIo* service);

  static bool transmitPacket(packet::PacketPtr packet, uint32_t ip, uint16_t port);
  static bool transmitPacket(packet::PacketPtr packet, const char* ip, uint16_t port);

  static bool getEndpoint(char* ip, size_t ip_len, uint16_t* port);

  static bool start();
};
}  // namespace xbot::service

#endif  // IO_H
