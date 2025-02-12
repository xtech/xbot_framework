//
// Created by clemens on 7/16/24.
//

#include "PlotJugglerBridge.hpp"

#include "spdlog/spdlog.h"

using namespace xbot::serviceif;

// Function pointer for our raw conversion functions
typedef nlohmann::json (*ToJson)(const void *, size_t);

nlohmann::json CharToJson(const void *data, size_t len) {
  auto c_str = static_cast<const char *>(data);
  size_t effectiveLength = std::min(len, strnlen(c_str, len));
  return std::string{c_str, effectiveLength};
}

nlohmann::json UIntToJson(const void *data, size_t len) {
  switch (len) {
    case sizeof(uint8_t): {
      auto ptr = static_cast<const uint8_t *>(data);
      return *ptr;
    }
    case sizeof(uint16_t): {
      auto ptr = static_cast<const uint16_t *>(data);
      return *ptr;
    }
    case sizeof(uint32_t): {
      auto ptr = static_cast<const uint32_t *>(data);
      return *ptr;
    }
    default: throw std::runtime_error("Invalid unsigned int size.");
  }
}

nlohmann::json IntToJson(const void *data, size_t len) {
  switch (len) {
    case sizeof(int8_t): {
      auto ptr = static_cast<const int8_t *>(data);
      return *ptr;
    }
    case sizeof(int16_t): {
      auto ptr = static_cast<const int16_t *>(data);
      return *ptr;
    }
    case sizeof(int32_t): {
      auto ptr = static_cast<const int32_t *>(data);
      return *ptr;
    }
    default: throw std::runtime_error("Invalid int size.");
  }
}

nlohmann::json FloatToJson(const void *data, size_t len) {
  switch (len) {
    case sizeof(float): {
      auto *ptr = static_cast<const float *>(data);
      return *ptr;
    }
    case sizeof(double): {
      auto *ptr = static_cast<const double *>(data);
      return *ptr;
    }
    default: throw std::runtime_error("Invalid float size.");
  }
}

/**
 * Supported types for RAW -> JSON translation
 */
const std::map<std::string, ToJson> conversion_fn_map{
    {"char", CharToJson},     {"uint8_t", UIntToJson}, {"uint16_t", UIntToJson},
    {"uint32_t", UIntToJson}, {"int8_t", IntToJson},   {"int16_t", IntToJson},
    {"int32_t", IntToJson},   {"float", FloatToJson},  {"double", FloatToJson},
};
const std::map<std::string, size_t> type_size_map{
    {"char", sizeof(char)},         {"uint8_t", sizeof(uint8_t)}, {"uint16_t", sizeof(uint16_t)},
    {"uint32_t", sizeof(uint32_t)}, {"int8_t", sizeof(int8_t)},   {"int16_t", sizeof(int16_t)},
    {"int32_t", sizeof(int32_t)},   {"float", sizeof(float)},     {"double", sizeof(float)},
};

PlotJugglerBridge::~PlotJugglerBridge() {
  ctx.io->UnregisterCallbacks(this);
}

bool PlotJugglerBridge::OnServiceDiscovered(uint16_t service_id) {
  // Query the service description and add build the map
  std::unique_lock lk{state_mutex_};

  const auto info = ctx.serviceDiscovery->GetServiceInfo(service_id);
  for (const auto &output : info->description.outputs) {
    topic_map_[std::make_pair(service_id, output.id)] = output;
  }

  // We are interested in all discovered services, so we register us with the
  // ServiceIO as soon as a new service is discovered
  ctx.io->RegisterCallbacks(service_id, this);
  return true;
}

bool PlotJugglerBridge::OnEndpointChanged(uint16_t service_id, uint32_t old_ip, uint16_t old_port, uint32_t new_ip,
                                          uint16_t new_port) {
  // We don't care, ServiceIO will handle this for us.
  return false;
}

void PlotJugglerBridge::OnServiceConnected(uint16_t service_id) {
  spdlog::info("PJB: Service Connected! ID: {}", service_id);
}

void PlotJugglerBridge::OnData(uint16_t service_id, uint64_t timestamp, uint16_t target_id, const void *payload,
                               size_t buflen) {
  std::unique_lock lk{state_mutex_};
  // Find the output description
  const auto &it = topic_map_.find(std::make_pair(service_id, target_id));
  if (it == topic_map_.end()) {
    spdlog::warn("PJB: Data packet with invalid ID");
    return;
  }

  const auto &output = it->second;

  nlohmann::json data;
  if (output.encoding == "zcbor") {
    // Try parse the CBOR and send it
    try {
      // Build the JSON and send it
      data = nlohmann::json::from_cbor(static_cast<const uint8_t *>(payload), false);
    } catch (std::exception &e) {
      spdlog::warn("Exception parsing CBOR data: {}", e.what());
    }
  } else if (output.encoding.empty() || output.encoding == "raw") {
    // Raw encoding, find conversion function and call it
    if (!conversion_fn_map.contains(output.type)) {
      spdlog::error("Error, conversion function for type {}", output.type);
      return;
    }
    const auto &fn = conversion_fn_map.at(output.type);
    if (!output.is_array) {
      // convert
      data = fn(payload, buflen);
    } else {
      // Get the itme size
      if (!type_size_map.contains(output.type)) {
        spdlog::error("Error, no size info for type {}", output.type);
        return;
      }
      size_t item_size = type_size_map.at(output.type);
      size_t array_len = std::min(buflen / item_size, static_cast<size_t>(output.maxlen));
      for (size_t i = 0; i < array_len; i++) {
        data[i] = fn(static_cast<const uint8_t *>(payload) + i * item_size, item_size);
      }
    }
  }

  try {
    nlohmann::json json =
        nlohmann::json::object({{std::to_string(service_id), {{output.name, {{"stamp", timestamp}, {"data", data}}}}}});
    std::string dump = json.dump(2);
    socket_.TransmitPacket("127.0.0.1", 9870, reinterpret_cast<const uint8_t *>(dump.c_str()), dump.size());
  } catch (std::exception &e) {
    spdlog::error("PJB: Error encoding JSON: {}", e.what());
  }
}

void PlotJugglerBridge::OnServiceDisconnected(uint16_t service_id) {
  spdlog::info("PJB: OnServiceDisconnected. ID: {}", service_id);
}

PlotJugglerBridge::PlotJugglerBridge(xbot::serviceif::Context ctx) : ctx(ctx) {
}

bool PlotJugglerBridge::Start() {
  if (!socket_.Start()) return false;
  ctx.serviceDiscovery->RegisterCallbacks(this);
  return true;
}
