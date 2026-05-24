//
// Created by clemens on 7/21/24.
//
#include <spdlog/spdlog.h>

#include <xbot-service-interface/XbotServiceInterface.hpp>

#include "EchoServiceInterface.hpp"

using namespace xbot::serviceif;

int main() {
  Context ctx = Start();

  // Instantiate the interface to the EchoService.
  // It will be discovered and bound automatically.
  // In order for this to work, start an EchoService anywhere in your
  // network. This can be the Linux example or an implementation on your
  // Microcontroller.
  EchoServiceInterface si{1, ctx};
  si.Start();
  int i = 0;

  while (ctx.io->OK()) {
    std::string input_text = "Echo Request " + std::to_string(i++);
    // Send a message to the service. The echo will be printed by the
    // EchoServiceInterface implementation.
    si.SendInputText(input_text.c_str(), input_text.length());
    // Wait a second.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    std::string input="test!";
    char result[50]{};
    uint16_t result_size=sizeof(result);
    bool success = si.CallRpcEchoTest(input.c_str(),input.length(), i, result, result_size);
    if (success) {
      std::string result_str(result, result_size);
      spdlog::info("RPC Echo Test Result: {}, size: {}", result_str, result_size);
    } else {
      spdlog::error("RPC error");
    }
  }
}
