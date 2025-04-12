//
// Created by clemens on 4/23/24.
//

#ifndef ECHOSERVICE_HPP
#define ECHOSERVICE_HPP

#include "EchoServiceBase.hpp"

using namespace xbot::service;

class EchoService : public EchoServiceBase {
 public:
  explicit EchoService(uint16_t service_id) : EchoServiceBase(service_id) {
  }

 private:
  void tick();
  ManagedSchedule tick_schedule_{scheduler_, IsRunning(), 1'000'000,
                                 XBOT_FUNCTION_FOR_METHOD(EchoService, &EchoService::tick, this)};

  uint32_t echo_count = 0;

 protected:
  void OnInputTextChanged(const char *new_value, uint32_t length) override;
  bool OnStart() override;
  void OnStop() override;
};

#endif  // ECHOSERVICE_HPP
