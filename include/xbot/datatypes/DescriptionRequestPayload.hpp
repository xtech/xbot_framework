//
// Payload of a DESCRIPTION_REQUEST.
//

#ifndef DESCRIPTION_REQUEST_PAYLOAD_HPP
#define DESCRIPTION_REQUEST_PAYLOAD_HPP

#include <cstdint>

namespace xbot::datatypes {

/**
 * @brief Where the service should send the DESCRIPTION_PART to.
 *
 * The portable packet API only hands out the payload, not the sender's address, so the
 * host brings its own endpoint along - the same idea as ClaimPayload.
 */
struct DescriptionRequestPayload {
  uint16_t service_id{};
  uint32_t target_ip{};
  uint16_t target_port{};
} __attribute__((packed));

}  // namespace xbot::datatypes

#endif  // DESCRIPTION_REQUEST_PAYLOAD_HPP
