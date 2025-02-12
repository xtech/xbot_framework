//
// Created by clemens on 07.02.25.
//

#ifndef REMOTELOGGINGRECEIVERIMPL_HPP
#define REMOTELOGGINGRECEIVERIMPL_HPP
#include <mutex>
#include <string>
#include <thread>
#include <xbot-service-interface/Socket.hpp>
#include <xbot/config.hpp>

namespace xbot::serviceif {

class RemoteLoggingReceiverImpl {
 private:
  std::thread rl_thread_{};
  Socket rl_socket_{config::remote_log_multicast_address, config::multicast_port};
  std::mutex stopped_mtx_{};
  bool stopped_{false};
  static RemoteLoggingReceiverImpl* instance_;

  static std::mutex instance_mtx_;
  void Run();

 public:
  bool Start();
  static void SetMulticastIfAddress(std::string multicast_if_address);
  static RemoteLoggingReceiverImpl* GetInstance();
  bool Stop();
};

}  // namespace xbot::serviceif

#endif  // REMOTELOGGINGRECEIVERIMPL_HPP
