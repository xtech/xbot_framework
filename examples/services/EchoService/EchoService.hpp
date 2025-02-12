//
// Created by clemens on 4/23/24.
//

#ifndef ECHOSERVICE_HPP
#define ECHOSERVICE_HPP

#include "EchoServiceBase.hpp"

class EchoService : public EchoServiceBase {
 public:
  explicit EchoService(uint16_t service_id) : EchoServiceBase(service_id, 1000000) {
  }

 private:
  void tick() override;

  uint32_t echo_count = 0;

 protected:
  void OnInputTextChanged(const char *new_value, uint32_t length) override;
  void OnCreate() override;
  bool Configure() override;
  void OnStart() override;
  void OnStop() override;
};

#endif  // ECHOSERVICE_HPP
