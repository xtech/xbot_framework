#include <xbot-service/DataSource.hpp>

namespace xbot::service {

bool HeatshrinkDataSource::Decode() {
  size_t processed_now, polled_now;
  HSD_poll_res poll_res;
  buf_pos_ = 0;
  buf_size_ = 0;
  while (compressed_pos_ < size_) {
    heatshrink_decoder_sink(&decoder_, const_cast<uint8_t*>(&data_[compressed_pos_]), size_ - compressed_pos_,
                            &processed_now);
    compressed_pos_ += processed_now;

    do {
      poll_res = heatshrink_decoder_poll(&decoder_, &buf_[buf_size_], sizeof(buf_) - buf_size_, &polled_now);
      buf_size_ += polled_now;
      if (buf_size_ == sizeof(buf_)) return true;
    } while (poll_res == HSDR_POLL_MORE);
  }

  while (heatshrink_decoder_finish(&decoder_) == HSDR_FINISH_MORE) {
    heatshrink_decoder_poll(&decoder_, &buf_[buf_size_], sizeof(buf_) - buf_size_, &polled_now);
    buf_size_ += polled_now;
    if (buf_size_ == sizeof(buf_)) return true;
  }

  return buf_size_ > 0;
}

}  // namespace xbot::service
