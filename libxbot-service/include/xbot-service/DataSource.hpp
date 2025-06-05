#include <cstddef>
#include <cstdint>

extern "C" {
#include <heatshrink_decoder.h>
}

namespace xbot::service {

class DataSource {
 public:
  DataSource(const uint8_t* data, const size_t size) : data_(data), size_(size) {
  }

  virtual bool HasNext() = 0;
  virtual uint8_t Next() = 0;
  virtual void Rewind() = 0;
  virtual size_t Position() {
    return pos_;
  };

 protected:
  const uint8_t* data_;
  const size_t size_;
  size_t pos_ = 0;
};

class RawDataSource : public DataSource {
 public:
  using DataSource::DataSource;

  bool HasNext() override {
    return pos_ < size_;
  }

  uint8_t Next() override {
    return data_[pos_++];
  }

  void Rewind() override {
    pos_ = 0;
  }
};

class HeatshrinkDataSource : public DataSource {
 public:
  using DataSource::DataSource;

  bool HasNext() override {
    return buf_pos_ < buf_size_ || Decode();
  }

  uint8_t Next() override {
    pos_++;
    return buf_[buf_pos_++];
  }

  void Rewind() override {
    heatshrink_decoder_reset(&decoder_);
    pos_ = 0;
    compressed_pos_ = 0;
    buf_pos_ = 0;
    buf_size_ = 0;
  }

 private:
  heatshrink_decoder decoder_;
  size_t compressed_pos_;
  uint8_t buf_[32];
  size_t buf_pos_;
  size_t buf_size_;

  bool Decode();
};

}  // namespace xbot::service
