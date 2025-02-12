//
// Created by clemens on 4/30/24.
//

#ifndef CROWTOSPEEDLOGHANDLER_HPP
#define CROWTOSPEEDLOGHANDLER_HPP

#include <spdlog/spdlog.h>

namespace xbot::hub {
/**
 * @class CrowToSpeedlogHandler
 * @brief A class that logs messages from Crow to spdlog
 */
class CrowToSpeedlogHandler : public crow::ILogHandler {
 public:
  CrowToSpeedlogHandler() = default;

  void log(std::string message, crow::LogLevel level) {
    // "message" doesn't contain the timestamp and loglevel
    // prefix the default logger does and it doesn't end
    // in a newline.
    switch (level) {
      case crow::LogLevel::DEBUG: spdlog::debug("[CROW]: {}", message); break;
      case crow::LogLevel::INFO: spdlog::info("[CROW]: {}", message); break;
      case crow::LogLevel::WARNING: spdlog::warn("[CROW]: {}", message); break;
      case crow::LogLevel::ERROR: spdlog::error("[CROW]: {}", message); break;
      case crow::LogLevel::CRITICAL:
      default: spdlog::critical("[CROW]: {}", message);
    }
  }
};
}  // namespace xbot::hub

#endif  // CROWTOSPEEDLOGHANDLER_HPP
