//
// Created by clemens on 3/25/24.
//

#ifndef HEADER_HPP
#define HEADER_HPP

#include <cstdint>

namespace xbot::datatypes {
enum class MessageType : uint8_t {
  // First bit 0, the payload is "raw".
  // Use for simple, but frequent messages (e.g. sensor data, ack messages,
  // ...).
  UNKNOWN = 0x00,
  // Use DATA for inputs and outputs.
  DATA = 0x01,
  // Use CONFIGURATION_REQUEST is sent by the service when it needs
  // configuration
  CONFIGURATION_REQUEST = 0x02,
  // User CLAIM in order to claim a service. Payload is IP (uint32_t) and
  // port(uint16_t). Service will reply with CLAM with arg1 == true for ack
  CLAIM = 0x03,
  // Heartbeat is sent regularly by service to show that its alive
  HEARTBEAT = 0x04,
  // Transaction bundles multiple data IOs separated with
  // DataDescriptor headers.
  TRANSACTION = 0x05,
  // For remote debug logging
  LOG = 0x7F,
  // First bit 1, the payload is JSON encoded.
  // Use for complex, infrequent, non-realtime critical messages (e.g. service
  // discovery).
  SERVICE_ADVERTISEMENT = 0x80,
  SERVICE_QUERY = 0x81
};

#pragma pack(push, 1)
struct XbotHeader {
  // Version of the protocol, increment on breaking changes.
  // 1 = initial release
  uint8_t protocol_version{};
  // 8 bit message type
  MessageType message_type{};
  // Flags.
  // Bit 0: Reboot (1 after service started, 0 after sequence_no rolled over at
  // least once)
  uint8_t flags{};
  uint8_t reserved1{};
  uint16_t service_id{};
  // Reserved for message specific payload (e.g. log level for log message)
  // Log Message: Log level
  // Transaction: Type: 0 = Data transaction, 1 = Configuration Transaction
  uint8_t arg1{};
  uint8_t reserved2{};
  // Reserved for message specific payload (e.g. target_id for data message)
  uint16_t arg2{};
  // Sequence number, increment on each message. Clear "reboot" flag on roll
  // over
  uint16_t sequence_no{};
  // Message timestamp. Unix timestamp in nanoseconds.
  // Nanoseconds because we need 64 bit anyways and even with nanoseconds we
  // have until year 2554 before it rolls over.
  uint64_t timestamp{};
  // Length of payload in bytes. Especially important for "raw" messages.
  // Also this allows us to chain multiple xBot packets into a single UDP
  // packet.
  uint32_t payload_size{};
} __attribute__((packed));
#pragma pack(pop)

#pragma pack(push, 1)
struct DataDescriptor {
  // Target ID for the next piece of data
  uint16_t target_id{};
  // Reserved for alignment and future use
  uint16_t reserved{};
  // Length of next payload in bytes.
  uint32_t payload_size{};
} __attribute__((packed));
#pragma pack(pop)
}  // namespace xbot::datatypes

#endif  // HEADER_HPP
