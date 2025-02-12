//
// Created by clemens on 7/16/24.
//

#ifndef SERVICEIOINFO_HPP
#define SERVICEIOINFO_HPP

#include <regex>

struct ServiceIOInfo {
  // Id of this input or output (used to send and receive data)
  uint16_t id{};

  // Name of this input or output e.g. "speed"
  std::string name{};

  // Type e.g. "uint32_t" or "char[100]"
  std::string type{};

  // Optional string specifying the encoding (e.g. zcbor, or raw if none is
  // given)
  std::string encoding{};

  // True, if array type
  bool is_array{false};

  // maxlen if array tupe
  uint32_t maxlen{};
};

inline void to_json(nlohmann::json &nlohmann_json_j, const ServiceIOInfo &nlohmann_json_t) {
  nlohmann_json_j["id"] = nlohmann_json_t.id;
  nlohmann_json_j["name"] = nlohmann_json_t.name;

  if (!nlohmann_json_t.is_array) {
    nlohmann_json_j["type"] = nlohmann_json_t.type;
  } else {
    nlohmann_json_j["type"] = nlohmann_json_t.type + "[" + std::to_string(nlohmann_json_t.maxlen) + "]";
  }
  if (!nlohmann_json_t.encoding.empty()) {
    nlohmann_json_j["encoding"] = nlohmann_json_t.encoding;
  }
}

inline void from_json(const nlohmann::json &nlohmann_json_j, ServiceIOInfo &nlohmann_json_t) {
  nlohmann_json_j.at("id").get_to(nlohmann_json_t.id);
  nlohmann_json_j.at("name").get_to(nlohmann_json_t.name);
  const std::string &type_str = nlohmann_json_j.at("type");

  // Parse the type
  std::regex r(R"(([a-z0-9_]+)(\[(\d+)\])?)");
  std::smatch match;
  if (std::regex_search(type_str, match, r)) {
    nlohmann_json_t.type = match[1];
    if (match[2].matched) {  // Array part "[100]" was found
      nlohmann_json_t.is_array = true;
      nlohmann_json_t.maxlen = std::stoi(match[3]);
    } else {
      nlohmann_json_t.is_array = false;
      nlohmann_json_t.maxlen = 0;
    }
  } else {
    throw std::runtime_error("Error parsing type");
  }

  if (nlohmann_json_j.contains("encoding")) {
    nlohmann_json_j.at("encoding").get_to(nlohmann_json_t.encoding);
  }
}
#endif  // SERVICEIOINFO_HPP
