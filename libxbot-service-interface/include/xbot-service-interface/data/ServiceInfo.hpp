//
// Created by clemens on 4/29/24.
//

#ifndef SERVICEINFO_HPP
#define SERVICEINFO_HPP

#include <nlohmann/json.hpp>
#include <string>
#include <xbot-service-interface/endpoint_utils.hpp>

#include "ServiceDescription.hpp"

namespace xbot::serviceif {
struct ServiceInfo {
 public:
  ServiceInfo() = default;

  uint16_t service_id_{};

  // Endpoint info on where to reach the service
  uint32_t ip{};
  uint16_t port{};

  ServiceDescription description{};
};

inline void to_json(nlohmann::json &j, const ServiceInfo &si) {
  j = nlohmann::json{{"sid", si.service_id_},
                     {
                         "endpoint",
                         {{"ip", IpIntToString(si.ip)}, {"port", si.port}},
                     },
                     {"desc", si.description}};
}

inline void from_json(const nlohmann::json &j, ServiceInfo &p) {
  j.at("sid").get_to(p.service_id_);
  p.ip = IpStringToInt(j.at("endpoint").at("ip").get<std::string>());
  j.at("endpoint").at("port").get_to(p.port);
  j.at("desc").get_to(p.description);
}
}  // namespace xbot::serviceif

#endif  // SERVICEINFO_HPP
