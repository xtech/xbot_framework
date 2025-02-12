//
// Created by clemens on 3/20/24.
//

#ifndef SOCKET_HPP
#define SOCKET_HPP
#include <cstddef>
#include <cstdint>
#include <xbot-service/portable/packet.hpp>
#include <xbot/socket_impl.hpp>

#ifndef XBOT_SOCKET_TYPEDEF
#error XBOT_SOCKET_TYPEDEF undefined
#endif

namespace xbot::service::sock {
typedef XBOT_SOCKET_TYPEDEF* SocketPtr;

/**
 * Creates a socket and returns the pointer to it
 * @param bind_multicast: true to prepare the socket for listening
 * @return The created socket
 */
bool initialize(SocketPtr socket, bool bind_multicast);

/**
 * Frees all socket resources
 */
void deinitialize(SocketPtr socket);

/**
 * Subscribe to a streaming channel.
 * This will tell the socket to receive data on this channel and the socket will
 * provide the data using the receive_packet() function.
 * @param socket The socket
 * @param ip the IP address to subscribe to
 * @return true on success
 */
bool subscribeMulticast(SocketPtr socket, const char* ip);

/**
 * Blocks until packet is received or timeout occured.
 *
 * @param socket The socket
 * @param timeout_millis The timeout in milliseconds
 * @param packet A pointer to a packet pointer.
 *
 * @return true, if a packet was received
 */
bool receivePacket(SocketPtr socket, packet::PacketPtr* packet);

/**
 * Transmits a packet to a channel using the provided socket.
 * The packet will be freed by the driver, don't free the packet yourself.
 *
 * @param socket socket to use for transmission
 * @param packet packet to transmit
 * @param ip the ip to transmit to
 * @param port the port to transmit to
 * @return true on success
 */
bool transmitPacket(SocketPtr socket, packet::PacketPtr packet, const char* ip, uint16_t port);
bool transmitPacket(SocketPtr socket, packet::PacketPtr packet, uint32_t ip, uint16_t port);

/**
 * Closes the socket.
 * @return true, if close was success
 */
bool closeSocket(SocketPtr socket);

/**
 * Get the IP Address and port where this socket is bound to.
 * @param ip: pointer to char array where the IP should be put. Length needs to
 * be at least 16 bytes (to fit 255.255.255.255 + terminating 0)
 * @param ip_len: length of ip buffer
 * @param port: pointer to the uint16_t where the port will be stored
 * @return true, on success.
 */
bool getEndpoint(SocketPtr socket, char* ip, size_t ip_len, uint16_t* port);
}  // namespace xbot::service::sock

#endif  // SOCKET_HPP
