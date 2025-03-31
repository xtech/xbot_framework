//
// Created by clemens on 4/23/24.
//

#ifndef ECHOSERVICE_HPP
#define ECHOSERVICE_HPP

#include "EchoServiceBase.hpp"
#include "etl/callback_timer.h"

class EchoService : public EchoServiceBase {
 public:
  explicit EchoService(uint16_t service_id) : EchoServiceBase(service_id, 1'000) {
  }

 private:
  void tick() override;

void timer_callback1();
void timer_callback2();

  uint32_t echo_count = 0;

  etl::callback_timer<5> callback_timer_{};

  etl::delegate<void()> timer1_delegate_;
  etl::delegate<void()> timer2_delegate_;

 protected:
  void OnCreate() override;
  void OnInputTextChanged(const char *new_value, uint32_t length) override;
  bool OnStart() override;
  void OnStop() override;
};

#endif  // ECHOSERVICE_HPP
