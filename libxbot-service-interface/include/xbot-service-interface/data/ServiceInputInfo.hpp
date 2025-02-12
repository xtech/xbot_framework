//
// Created by clemens on 4/29/24.
//

#ifndef SERVICEINPUTINFO_HPP
#define SERVICEINPUTINFO_HPP
#include <string>
#include <utility>

namespace xbot::serviceif {

class ServiceInputInfo {
 public:
  ServiceInputInfo() = default;

  ServiceInputInfo(const ServiceInputInfo &other) : id(other.id), name(other.name), type(other.type) {
  }

  ServiceInputInfo(ServiceInputInfo &&other) noexcept
      : id(other.id), name(std::move(other.name)), type(std::move(other.type)) {
  }

  ServiceInputInfo &operator=(const ServiceInputInfo &other) {
    if (this == &other) return *this;
    id = other.id;
    name = other.name;
    type = other.type;
    return *this;
  }

  ServiceInputInfo &operator=(ServiceInputInfo &&other) noexcept {
    if (this == &other) return *this;
    id = other.id;
    name = std::move(other.name);
    type = std::move(other.type);
    return *this;
  }

  ServiceInputInfo(uint16_t id, std::string name, std::string type)
      : id(id), name(std::move(name)), type(std::move(type)) {
  }

  // ID of the input (used for sending data to it)
  uint16_t id{};

  // Name of the input channel (e.g. "speed")
  std::string name{};

  // Type of the input channel (e.g. uint32_t or char[100])
  std::string type{};

  // Encoding: Optional string specifying the encoding (e.g. zcbor)
  std::string encoding{};
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ServiceInputInfo, id, name, type, encoding)
}  // namespace xbot::serviceif

#endif  // SERVICEINPUTINFO_HPP
