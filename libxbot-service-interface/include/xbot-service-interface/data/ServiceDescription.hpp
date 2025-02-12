//
// Created by clemens on 7/16/24.
//

#ifndef SERVICEDESCRIPTION_HPP
#define SERVICEDESCRIPTION_HPP

#include "ServiceIOInfo.hpp"

struct ServiceDescription {
  // Type of the service (e.g. IMU Service)
  std::string type{};

  // Version of the service (changes whenever the interface introduces breaking
  // changes)
  uint32_t version{};

  std::vector<ServiceIOInfo> inputs;
  std::vector<ServiceIOInfo> outputs;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ServiceDescription, type, version, inputs, outputs)

#endif  // SERVICEDESCRIPTION_HPP
