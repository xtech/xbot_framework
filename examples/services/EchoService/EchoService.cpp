//
// Created by clemens on 4/23/24.
//

#include "EchoService.hpp"

#include <cstring>
#include <iostream>

void EchoService::tick() {
  SendMessageCount(echo_count++);
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

bool EchoService::OnStart() {
  std::cout << "Service Started" << std::endl;
  return true;
}

void EchoService::OnStop() {
  std::cout << "Service Stopped" << std::endl;
}

void EchoService::RPCSetPrefix(uint16_t call_id, const char *Prefix, uint32_t PrefixLen) {
  if (PrefixLen > sizeof(this->Prefix.value)) {
    bool result = false;
    SendRpcResponse(call_id, xbot::datatypes::RpcStatus::SUCCESS, &result, sizeof(result));
    return;
  }
  memcpy(this->Prefix.value, Prefix, PrefixLen);
  this->Prefix.length = PrefixLen;
  bool result = true;
  SendRpcResponse(call_id, xbot::datatypes::RpcStatus::SUCCESS, &result, sizeof(result));
}

void EchoService::RPCResetCount(uint16_t call_id) {
  echo_count = 0;
  SendRpcResponse(call_id, xbot::datatypes::RpcStatus::SUCCESS, nullptr, 0);
}
