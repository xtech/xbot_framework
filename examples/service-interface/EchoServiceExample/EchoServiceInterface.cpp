//
// Created by clemens on 7/21/24.
//
#include "EchoServiceInterface.hpp"
EchoServiceInterface::EchoServiceInterface(uint16_t service_id, xbot::serviceif::Context ctx)
    : EchoServiceInterfaceBase(service_id, ctx) {
}
EchoServiceInterface::~EchoServiceInterface() {
}

bool EchoServiceInterface::OnConfigurationRequested(uint16_t uid) {
  spdlog::info("Config Requested");
  std::string prefix = "Example Prefix: ";

  // Send configuration to service
  StartTransaction(true);
  // Configure the service to use the provided prefix for each echo
  SetRegisterPrefix(prefix.c_str(), prefix.length());

  // Configure the service to send two echos for each request.
  SetRegisterEchoCount(2);
  CommitTransaction();

  // Return true to signal that this plugin took care of the configuration.
  return true;
}

void EchoServiceInterface::OnEchoChanged(const char* new_value, uint32_t length) {
  std::string e = std::string(new_value, length);
  spdlog::info("Got echo {}", e);
}
void EchoServiceInterface::OnMessageCountChanged(const uint32_t& new_value) {
  // Called whenever the MessageCount output changes.
}
