//
// Created by clemens on 4/23/24.
//

#include "EchoService.hpp"

#include <iostream>
#include <xbot-service/portable/system.hpp>
#include <ulog.h>

void EchoService::tick() {
  static uint32_t last_time_micros = 0;
  uint32_t now_micros = xbot::service::system::getTimeMicros();
  callback_timer_.tick(now_micros - last_time_micros);
  last_time_micros = now_micros;
}
void EchoService::timer_callback1() {
  SendMessageCount(echo_count++);
  ULOG_ARG_INFO(&service_id_, "timer callback 1");
}

void EchoService::timer_callback2() {
  ULOG_ARG_INFO(&service_id_, "timer callback 2");
}

void EchoService::OnInputTextChanged(const char *new_value, uint32_t length) {
  std::string input{new_value, length};
  std::cout << "Got message: " << input << std::endl;
  std::string response{Prefix.value, Prefix.length};
  response += input;
  for (int i = 0; i < EchoCount.value; i++) {
    SendEcho(response.c_str(), response.length());
  }

  SendMessageCount(echo_count++);
}

void EchoService::OnCreate() {
  timer1_delegate_ = etl::delegate<void()>::create<EchoService, &EchoService::timer_callback1>(*this);
  timer2_delegate_ = etl::delegate<void()>::create<EchoService, &EchoService::timer_callback2>(*this);
  const auto timer_id1 = callback_timer_.register_timer(timer1_delegate_, 1'000'000, true);
  const auto timer_id2 = callback_timer_.register_timer(timer2_delegate_, 10'000'000, true);
  callback_timer_.enable(true);
  callback_timer_.start(timer_id1);
  callback_timer_.start(timer_id2);
}

bool EchoService::OnStart() {
  std::cout << "Service Started" << std::endl;
  return true;
}

void EchoService::OnStop() {
  std::cout << "Service Stopped" << std::endl;
}
