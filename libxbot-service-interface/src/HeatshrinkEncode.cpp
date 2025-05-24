#include <xbot-service-interface/HeatshrinkEncode.hpp>

extern "C" {
#include <heatshrink_encoder.h>
}

std::vector<uint8_t> HeatshrinkEncode(uint8_t *data, const size_t size) {
  heatshrink_encoder *encoder = heatshrink_encoder_alloc(8, 4);
  std::vector<uint8_t> out;
  uint8_t buf[128];
  size_t processed = 0, processed_now, polled_now;
  HSE_poll_res poll_res;

  while (processed < size) {
    heatshrink_encoder_sink(encoder, &data[processed], size - processed, &processed_now);
    processed += processed_now;

    do {
      poll_res = heatshrink_encoder_poll(encoder, buf, sizeof(buf), &polled_now);
      out.insert(out.end(), buf, buf + polled_now);
    } while (poll_res == HSER_POLL_MORE);
  }

  while (heatshrink_encoder_finish(encoder) == HSER_FINISH_MORE) {
    heatshrink_encoder_poll(encoder, buf, sizeof(buf), &polled_now);
    out.insert(out.end(), buf, buf + polled_now);
  }

  return out;
}
