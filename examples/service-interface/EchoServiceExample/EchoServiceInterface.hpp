//
// Created by clemens on 7/21/24.
//

#ifndef XBOT_FRAMEWORK_ECHOSERVICEINTERFACE_HPP
#define XBOT_FRAMEWORK_ECHOSERVICEINTERFACE_HPP

#include <spdlog/spdlog.h>

#include <EchoServiceInterfaceBase.hpp>

class EchoServiceInterface : public EchoServiceInterfaceBase {
 public:
  explicit EchoServiceInterface(uint16_t service_id, xbot::serviceif::Context ctx);

  ~EchoServiceInterface() override;

 protected:
 public:
  bool OnConfigurationRequested(uint16_t service_id) override;

 protected:
  void OnEchoChanged(const char *new_value, uint32_t length) override;
  void OnMessageCountChanged(const uint32_t &new_value) override;
};

#endif  // XBOT_FRAMEWORK_ECHOSERVICEINTERFACE_HPP
