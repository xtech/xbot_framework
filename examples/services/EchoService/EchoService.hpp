//
// Created by clemens on 4/23/24.
//

#ifndef ECHOSERVICE_HPP
#define ECHOSERVICE_HPP

#include "EchoServiceBase.hpp"

using namespace xbot::service;

class EchoService : public EchoServiceBase {
 protected:
  void RPCRpcEchoTest(uint16_t call_id, const char* Text, uint32_t TextLen, const uint32_t& EchoCount, char* data,
                      uint16_t* response_length) override;

 public:
  explicit EchoService(uint16_t service_id) : EchoServiceBase(service_id) {
  }

 private:
  void tick();
  ServiceSchedule tick_schedule_{*this, 1'000'000, XBOT_FUNCTION_FOR_METHOD(EchoService, &EchoService::tick, this)};

  uint32_t echo_count = 0;

 protected:
  void OnInputTextChanged(const char *new_value, uint32_t length) override;
  bool OnStart() override;
  void OnStop() override;
  void RPCSetPrefix(uint16_t call_id, const char *Prefix, uint32_t PrefixLen) override;
  void RPCResetCount(uint16_t call_id) override;
};

#endif  // ECHOSERVICE_HPP
