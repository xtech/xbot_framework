//
// Created by clemens on 4/23/24.
//

#include "EchoService.hpp"

#include <iostream>

void EchoService::tick() { SendMessageCount(echo_count++); }

bool EchoService::OnInputTextChanged(const char *new_value, uint32_t length) {
  std::string input{new_value, length};
  std::cout << "Got message: " << input << std::endl;
  std::string response{Prefix.value, Prefix.length};
  response += input;
  for (int i = 0; i < EchoCount.value; i++) {
    SendEcho(response.c_str(), response.length());
  }

  SendMessageCount(echo_count++);
  return true;
}
void EchoService::OnCreate() {}
bool EchoService::Configure() {
  // Nothing to do on configure hook
  std::cout << "Service Configured" << std::endl;
  return true;
}
void EchoService::OnStart() { std::cout << "Service Started" << std::endl; }
void EchoService::OnStop() { std::cout << "Service Stopped" << std::endl; }
