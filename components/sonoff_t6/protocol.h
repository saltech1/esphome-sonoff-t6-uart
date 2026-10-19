#pragma once

// Independent of ESPHome so the actual parser can be tested on a host machine.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::sonoff_t6 {

static constexpr size_t MAX_PAYLOAD = 32;
static constexpr size_t MAX_FRAME = MAX_PAYLOAD + 6;
struct Frame {
  std::array<uint8_t, MAX_FRAME> bytes{};
  size_t size{0};
  uint8_t command() const { return bytes[3]; }
  uint8_t length() const { return bytes[4]; }
  uint8_t payload(size_t index) const { return bytes[5 + index]; }
};
enum class ParseResult { INCOMPLETE, FRAME, BAD_VERSION, BAD_LENGTH, BAD_CHECKSUM };
struct TouchEvent { uint8_t button_id; bool pressed; };
inline bool decode_touch(const Frame &frame, TouchEvent &event) {
  if (frame.size != 8 || frame.command() != 0x02 || frame.length() != 2 ||
      frame.payload(0) < 1 || frame.payload(0) > 3 || frame.payload(1) > 1) return false;
  event = {frame.payload(0), frame.payload(1) == 1};
  return true;
}

class PacketParser {
 public:
  void push(uint8_t byte) {
    // Defensive bound even if the caller neglects to drain next().
    if (size_ == buffer_.size()) discard_(1);
    buffer_[size_++] = byte;
  }
  size_t size() const { return size_; }
  void clear() { size_ = 0; }
  ParseResult next(Frame &frame) {
    while (size_ != 0) {
      if (buffer_[0] != 0xAA) { discard_(1); continue; }
      if (size_ < 2) return ParseResult::INCOMPLETE;
      if (buffer_[1] != 0x55) { discard_(1); continue; }
      if (size_ < 3) return ParseResult::INCOMPLETE;
      if (buffer_[2] != 0x01) {
        snapshot_(frame, size_);
        discard_(1);
        return ParseResult::BAD_VERSION;
      }
      if (size_ < 5) return ParseResult::INCOMPLETE;
      if (buffer_[4] > MAX_PAYLOAD) {
        snapshot_(frame, size_);
        discard_(1);
        return ParseResult::BAD_LENGTH;
      }
      const size_t length = buffer_[4] + 6;
      if (size_ < length) return ParseResult::INCOMPLETE;
      uint8_t checksum = 0;
      for (size_t i = 0; i < length - 1; i++) checksum += buffer_[i];
      snapshot_(frame, length);
      if (checksum != buffer_[length - 1]) {
        // Sliding resynchronization preserves a later valid AA 55 header.
        discard_(1);
        return ParseResult::BAD_CHECKSUM;
      }
      discard_(length);
      return ParseResult::FRAME;
    }
    return ParseResult::INCOMPLETE;
  }

 protected:
  std::array<uint8_t, MAX_FRAME> buffer_{};
  size_t size_{0};
  void snapshot_(Frame &frame, size_t length) const {
    frame.size = length;
    std::memcpy(frame.bytes.data(), buffer_.data(), length);
  }
  void discard_(size_t length) {
    size_ -= length;
    if (size_ != 0) std::memmove(buffer_.data(), buffer_.data() + length, size_);
  }
};

}  // namespace esphome::sonoff_t6
