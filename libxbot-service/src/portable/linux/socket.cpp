//
// Created by clemens on 3/21/24.
//
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <xbot-service/portable/socket.hpp>

#include "xbot/config.hpp"

using namespace xbot::service::sock;
using namespace xbot::service::packet;

bool get_ip(char* ip, size_t ip_len) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return false;
  }

  bool success = false;

  for (int iface_index = 1;; iface_index++) {
    ifreq ifr{};
    ifr.ifr_ifindex = iface_index;

    // The end
    if (ioctl(fd, SIOCGIFNAME, &ifr) < 0) {
      break;
    }

    // Skip loopback and virtual interfaces by checking interface name prefixes
    // This list was suggested by ChatGPT, not sure if it's complete, but it
    // looks good to me.
    if (strncmp(ifr.ifr_name, "lo", 2) == 0 || strncmp(ifr.ifr_name, "docker", 6) == 0 ||
        strncmp(ifr.ifr_name, "veth", 4) == 0 || strncmp(ifr.ifr_name, "virbr", 5) == 0 ||
        strncmp(ifr.ifr_name, "br-", 3) == 0 || strncmp(ifr.ifr_name, "wg", 2) == 0 ||
        strncmp(ifr.ifr_name, "tun", 3) == 0 || strncmp(ifr.ifr_name, "tap", 3) == 0) {
      continue;
    }

    // Get IP address
    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
      perror("SIOCGIFADDR");
      break;
    }

    const char* addrStr = inet_ntoa(reinterpret_cast<struct sockaddr_in*>(&ifr.ifr_addr)->sin_addr);
    if (strlen(addrStr) >= ip_len) {
      break;
    }

    strncpy(ip, addrStr, ip_len);
    success = true;
    break;
  }

  close(fd);
  return success;
}

bool xbot::service::sock::initialize(SocketPtr socket_ptr, bool bind_multicast) {
  *socket_ptr = -1;
  // Create a UDP socket

  int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    return false;
  }

  // Set receive timeout
  {
    timeval opt{};
    opt.tv_sec = 1;
    opt.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) < 0) {
      close(fd);
      return false;
    }
  }

  if (bind_multicast) {
    // Allow multiple binaries listening on the same socket.
    {
      int opt = 1;
      if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        close(fd);
        return false;
      }
    }

    // Make sure that multiple instances can talk to each other
    // This is done by enabling the loop back.
    // We need to make sure in the service, that the services don't process
    // their own messages though.
    {
      int opt = 1;
      if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &opt, sizeof(opt)) < 0) {
        close(fd);
        return false;
      }
    }

    sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_addr.s_addr = INADDR_ANY;
    saddr.sin_port = htons(config::multicast_port);

    // Bind to any address (i.e. 0.0.0.0:port)
    if (bind(fd, reinterpret_cast<sockaddr*>(&saddr), sizeof(saddr)) < 0) {
      close(fd);
      return false;
    }
  }

  // Create a pointer to the fd and return it.
  *socket_ptr = fd;
  return true;
}

void xbot::service::sock::deinitialize(SocketPtr socket) {
  if (socket != nullptr) {
    close(*socket);
  }
}

bool xbot::service::sock::subscribeMulticast(SocketPtr socket, const char* ip) {
  ip_mreq opt{};
  opt.imr_interface.s_addr = 0;
  opt.imr_multiaddr.s_addr = inet_addr(ip);

  if (setsockopt(*socket, IPPROTO_IP, IP_ADD_MEMBERSHIP, &opt, sizeof(opt)) < 0) {
    return false;
  }
  return true;
}

bool xbot::service::sock::receivePacket(SocketPtr socket, PacketPtr* packet) {
  const PacketPtr pkt = allocatePacket();
  sockaddr_in fromAddr{};
  socklen_t fromLen = sizeof(fromAddr);
  const ssize_t recvLen = recvfrom(*socket, pkt->buffer, config::max_packet_size, 0,
                                   reinterpret_cast<struct sockaddr*>(&fromAddr), &fromLen);
  if (recvLen < 0) {
    freePacket(pkt);
    return false;
  }
  pkt->used_data = recvLen;
  *packet = pkt;
  return true;
}

bool xbot::service::sock::transmitPacket(SocketPtr socket, PacketPtr packet, uint32_t ip, uint16_t port) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  sendto(*static_cast<int*>(socket), packet->buffer, packet->used_data, 0, reinterpret_cast<const sockaddr*>(&addr),
         sizeof(addr));

  freePacket(packet);

  return true;
}

bool xbot::service::sock::transmitPacket(SocketPtr socket, PacketPtr packet, const char* ip, uint16_t port) {
  return transmitPacket(socket, packet, ntohl(inet_addr(ip)), port);
}

bool xbot::service::sock::getEndpoint(SocketPtr socket, char* ip, size_t ip_len, uint16_t* port) {
  if (socket == nullptr || ip == nullptr || port == nullptr) return false;

  sockaddr_in addr{};
  socklen_t addrLen = sizeof(addr);

  if (getsockname(*static_cast<int*>(socket), reinterpret_cast<sockaddr*>(&addr), &addrLen) < 0) return false;

  if (addr.sin_addr.s_addr == 0) {
    // Socket bound to all interfaces, get primary IP
    if (!get_ip(ip, ip_len)) {
      return false;
    }
  } else {
    // Socket bound to single interface, use this IP
    const char* addrStr = inet_ntoa(addr.sin_addr);
    if (strlen(addrStr) >= ip_len) return false;

    strncpy(ip, addrStr, ip_len);
  }

  *port = ntohs(addr.sin_port);

  return true;
}

bool xbot::service::sock::closeSocket(SocketPtr socket) {
  if (socket == nullptr) return true;
  if (close(*socket) < 0) {
    return false;
  }
  return true;
}
