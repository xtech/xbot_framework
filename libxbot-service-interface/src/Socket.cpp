//
// Created by clemens on 4/29/24.
//

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <utility>
#include <xbot-service-interface/Socket.hpp>
#include <xbot/config.hpp>
using namespace xbot::serviceif;

bool get_ip(std::string &ip) {
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

    const char *addrStr = inet_ntoa(reinterpret_cast<struct sockaddr_in *>(&ifr.ifr_addr)->sin_addr);
    ip = addrStr;
    success = true;
    break;
  }

  close(fd);
  return success;
}

Socket::Socket(std::string bind_address, u_int16_t bind_port)
    : bind_ip_(std::move(bind_address)), bind_port_(bind_port) {
}

Socket::Socket(std::string bind_address) : Socket(std::move(bind_address), 0) {
}

bool Socket::Start() {
  // Create a UDP socket
  fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  // Bind Socket
  sockaddr_in saddr{};
  saddr.sin_family = AF_INET;
  saddr.sin_addr.s_addr = inet_addr(bind_ip_.c_str());
  saddr.sin_port = htons(bind_port_);

  if (bind(fd_, reinterpret_cast<sockaddr *>(&saddr), sizeof(saddr)) < 0) {
    close(fd_);
    fd_ = -1;
    return false;
  }

  // Set receive timeout
  {
    timeval opt{};
    opt.tv_sec = 1;
    opt.tv_usec = 0;
    if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) < 0) {
      close(fd_);
      fd_ = -1;
      return false;
    }
  }

  return true;
}
bool Socket::SetMulticastIfAddress(std::string multicast_if_address) {
  // Check, if Socket was started already
  if (fd_ != -1) return false;
  this->multicast_interface_address_ = std::move(multicast_if_address);
  return true;
}
bool Socket::SetBindAddress(std::string bind_address) {
  // Check, if Socket was started already
  if (fd_ != -1) return false;
  this->bind_ip_ = std::move(bind_address);
  return true;
}

bool Socket::JoinMulticast(std::string ip) {
  if (fd_ == -1) return false;
  ip_mreq opt{};
  opt.imr_interface.s_addr = inet_addr(multicast_interface_address_.c_str());
  opt.imr_multiaddr.s_addr = inet_addr(ip.c_str());

  if (setsockopt(fd_, IPPROTO_IP, IP_MULTICAST_IF, &opt.imr_interface, sizeof(opt.imr_interface)) < 0) {
    close(fd_);
    fd_ = -1;
    return false;
  }

  if (setsockopt(fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &opt, sizeof(opt)) < 0) {
    close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

bool Socket::ReceivePacket(uint32_t &sender_ip, uint16_t &sender_port, std::vector<uint8_t> &data) const {
  if (fd_ == -1) return false;
  data.clear();

  sockaddr_in fromAddr{};
  socklen_t fromLen = sizeof(fromAddr);

  data.resize(config::max_packet_size);

  const ssize_t recvLen =
      recvfrom(fd_, data.data(), data.size(), 0, reinterpret_cast<struct sockaddr *>(&fromAddr), &fromLen);
  if (recvLen < 0) {
    return false;
  }
  // Set the vector's size
  data.resize(recvLen);
  sender_ip = ntohl(fromAddr.sin_addr.s_addr);
  sender_port = ntohs(fromAddr.sin_port);
  return true;
}

bool Socket::TransmitPacket(uint32_t ip, uint16_t port, const std::vector<uint8_t> &data) const {
  if (fd_ == -1) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  sendto(fd_, data.data(), data.size(), 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));

  return true;
}

bool Socket::TransmitPacket(std::string ip, uint16_t port, const std::vector<uint8_t> &data) const {
  return TransmitPacket(ntohl(inet_addr(ip.c_str())), port, data);
}
bool Socket::TransmitPacket(uint32_t ip, uint16_t port, const uint8_t *data, size_t buflen) const {
  if (fd_ == -1) return false;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(ip);

  sendto(fd_, data, buflen, 0, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr));

  return true;
}
bool Socket::TransmitPacket(std::string ip, uint16_t port, const uint8_t *data, size_t buflen) const {
  return TransmitPacket(ntohl(inet_addr(ip.c_str())), port, data, buflen);
}

bool Socket::GetEndpoint(std::string &ip, uint16_t &port) const {
  if (fd_ == -1) return false;

  sockaddr_in addr{};
  socklen_t addrLen = sizeof(addr);

  if (getsockname(fd_, reinterpret_cast<sockaddr *>(&addr), &addrLen) < 0) return false;

  if (addr.sin_addr.s_addr == 0) {
    // Socket bound to all interfaces, get primary IP
    if (!get_ip(ip)) {
      return false;
    }
  } else {
    // Socket bound to single interface, use this IP
    const char *addrStr = inet_ntoa(addr.sin_addr);
    ip = addrStr;
  }

  port = ntohs(addr.sin_port);

  return true;
}

bool Socket::SetReceiveTimeoutMicros(uint32_t receive_timeout_micros) {
  timeval opt{};
  opt.tv_sec = receive_timeout_micros / 1'000'000;
  opt.tv_usec = receive_timeout_micros % 1'000'000;
  if (setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &opt, sizeof(opt)) < 0) {
    close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

Socket::~Socket() {
  if (fd_ != -1) {
    close(fd_);
    fd_ = -1;
  }
}
