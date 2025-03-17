//
// Created by clemens on 3/20/24.
//

#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <cstdint>
#include <type_traits>

// Check, that uint8_t is the same as unsigned char, so that we can cast points without violating
// the strict aliasing rules.
static_assert(std::is_same<uint8_t, unsigned char>::value, "uint8_t is not an alias for unsigned char");

namespace xbot::config {
static constexpr uint16_t max_packet_size = 1500;
static constexpr uint16_t max_service_count = 25;

[[maybe_unused]] static uint16_t multicast_port = 4242;

/**
 * Settings for remote logging
 */
[[maybe_unused]] static const char *remote_log_multicast_address = "233.255.255.1";
static constexpr uint16_t max_log_length = 255;

/**
 * Settings for Services
 */
// Max length for the name of a service
static constexpr uint16_t max_service_name_length = 50;

/**
 * Settings for Service Discovery
 */
// Max size in bytes for each service discovery entry.
// An entry can be a string (service name) or some other data type
// Make sure to keep this longer as your max_service_name_length+1 (for
// terminating 0).
static constexpr uint16_t max_sd_entry_size = 51;
// Make sure that we have at least space for a 64 bit integer.
static_assert(max_sd_entry_size > sizeof(uint64_t));
static_assert(max_sd_entry_size > max_service_name_length);
[[maybe_unused]] static const char *sd_multicast_address = "233.255.255.0";
static constexpr uint32_t sd_advertisement_interval_micros = 10'000'000;
// the fast sd advertisement time is used as long as the service is not yet
// claimed.
static constexpr uint32_t sd_advertisement_interval_micros_fast = 1'000'000;

// Time between request_configuration messages
static constexpr uint32_t request_configuration_interval_micros = 1'000'000;

/**
 * Default heartbeat value, a different value can be requested by the service
 * interface.
 */
static constexpr uint32_t default_heartbeat_micros = 1'000'000;
/**
 * Max micros to wait on top of default_heartbeat_micros before considering a
 * service disconnected
 */
static constexpr uint32_t heartbeat_jitter = 100'000;

static_assert(max_log_length > 100);

namespace service {
static constexpr uint32_t io_thread_stack_size = 5000;
}
}  // namespace xbot::config

#endif  // CONFIG_HPP
