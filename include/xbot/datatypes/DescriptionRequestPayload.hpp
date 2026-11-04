//
// Payload of a DESCRIPTION_REQUEST.
//

#ifndef DESCRIPTION_REQUEST_PAYLOAD_HPP
#define DESCRIPTION_REQUEST_PAYLOAD_HPP

#include <cstdint>

namespace xbot::datatypes {

/**
 * @brief Where the service should send the DESCRIPTION_PART to, and where in the omitted
 *        description this window starts.
 *
 * The portable packet API only hands out the payload, not the sender's address, so the
 * host brings its own endpoint along - the same idea as ClaimPayload.
 *
 * The offset lives here instead of in the header: `arg1` is one byte, and the omitted
 * description may be up to 64 KiB (the header's 16-bit total).
 */
struct DescriptionRequestPayload {
  uint16_t service_id{};
  uint32_t target_ip{};
  uint16_t target_port{};
  uint16_t offset{};
} __attribute__((packed));

}  // namespace xbot::datatypes

#endif  // DESCRIPTION_REQUEST_PAYLOAD_HPP
