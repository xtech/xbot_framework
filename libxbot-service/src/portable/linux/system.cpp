//
// Created by clemens on 3/25/24.
//

#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <ulog.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <xbot-service/portable/system.hpp>

#include "xbot-service/portable/mutex.hpp"

namespace xbot::service::system {
XBOT_MUTEX_TYPEDEF ulog_mutex_;

uint8_t node_id_[16]{};

/**
 * Get the MAC address for this node and use it as ID.
 * This will also work if you have multiple nodes running in the same system
 * as long as your service IDs are different.
 *
 * @return A unique ID
 */
void generateUniqueId() {
  memset(node_id_, 0, sizeof(node_id_));
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return;
  }

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

    // Get MAC address
    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
      perror("SIOCGIFHWADDR");
      break;
    }

    node_id_[0] = ifr.ifr_hwaddr.sa_data[0];
    node_id_[1] = ifr.ifr_hwaddr.sa_data[1];
    node_id_[2] = ifr.ifr_hwaddr.sa_data[2];
    node_id_[3] = ifr.ifr_hwaddr.sa_data[3];
    node_id_[4] = ifr.ifr_hwaddr.sa_data[4];
    node_id_[5] = ifr.ifr_hwaddr.sa_data[5];
    break;
  }

  close(fd);
}

void initSystem(uint32_t node_id) {
  xbot::service::mutex::initialize(&ulog_mutex_);

  // Init system with a hardcoded node_id or get a random one.
  if (node_id != 0) {
    node_id_[0] = node_id >> 8;
    node_id_[1] = node_id & 0xFF;
  } else {
    generateUniqueId();
  }

  bool zero_id = true;
  for (const uint8_t i : node_id_) {
    zero_id &= (i == 0);
  }
  if (zero_id) {
    printf("Error creating unique ID");
    exit(1);
  }

  // Init ULOG but don't register loggers.
  // This will be application dependend. E.g. a CLI app probably doesnt want us
  // to log to stdout by default. Also headless targets will want to log via
  // network.
  ULOG_INIT();
}

uint32_t getTimeMicros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool getNodeId(uint8_t *id, size_t id_len) {
  if (id_len != sizeof(node_id_)) return false;
  memcpy(id, node_id_, id_len);
  return true;
}
}  // namespace xbot::service::system

void ulog_lock_mutex() {
  xbot::service::mutex::lockMutex(&xbot::service::system::ulog_mutex_);
}

void ulog_unlock_mutex() {
  xbot::service::mutex::unlockMutex(&xbot::service::system::ulog_mutex_);
}
