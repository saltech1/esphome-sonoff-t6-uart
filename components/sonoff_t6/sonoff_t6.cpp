#include "sonoff_t6.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::sonoff_t6 {
static const char *const TAG = "sonoff_t6";

void SonoffT6::setup() {
  ESP_LOGI(TAG, "SONOFF T6 UART touch driver v1: receive-only; 115200 8N1");
  for (auto &entry : sensors_) entry.sensor->publish_initial_state(false);
}

void SonoffT6::handle_frame_(const Frame &frame) {
  ESP_LOGV(TAG, "RX %s", format_hex_pretty(frame.bytes.data(), frame.size).c_str());
  if (frame.command() != 0x02 || frame.length() != 2) {
    ESP_LOGD(TAG, "Unmapped frame: command=0x%02X payload_length=%u (no state change)",
             frame.command(), frame.length());
    return;
  }
  TouchEvent event;
  if (!decode_touch(frame, event)) {
    ESP_LOGD(TAG, "Unmapped touch: button=%u event=0x%02X (no state change)", frame.payload(0), frame.payload(1));
    return;
  }
  const uint8_t button = event.button_id;
  const bool pressed = event.pressed;
  ESP_LOGD(TAG, "Button ID %u: %s", button, pressed ? "PRESSED" : "RELEASED");
  pressed_[button - 1] = pressed;
  if (pressed) pressed_at_[button - 1] = millis();
  for (auto &entry : sensors_) {
    if (entry.button_id == button) entry.sensor->publish_state(pressed);
  }
}

void SonoffT6::loop() {
  const uint32_t now = millis();
  // Do not join a stale partial packet to a later transmission.
  if (parser_.size() != 0 && now - last_byte_ >= 200) {
    ESP_LOGW(TAG, "Discarding incomplete UART frame (%u bytes, 200ms timeout)",
             static_cast<unsigned>(parser_.size()));
    parser_.clear();
    rejected_++;
  }
  // Bound work if a noisy UART continuously delivers bytes.
  for (size_t count = 0; count < 128 && available() != 0; count++) {
    uint8_t byte;
    if (!read_byte(&byte)) {
      ESP_LOGW(TAG, "UART advertised data but read failed");
      break;
    }
    received_++;
    last_byte_ = millis();
    parser_.push(byte);
    Frame frame;
    ParseResult result;
    while ((result = parser_.next(frame)) != ParseResult::INCOMPLETE) {
      if (result == ParseResult::FRAME) {
        valid_++;
        handle_frame_(frame);
      } else {
        rejected_++;
        ESP_LOGW(TAG, "Rejected frame (%s): %s", result == ParseResult::BAD_CHECKSUM ? "checksum" :
                 (result == ParseResult::BAD_LENGTH ? "length >32" : "version !=1"),
                 format_hex_pretty(frame.bytes.data(), frame.size).c_str());
      }
    }
  }
  const uint32_t state_now = millis();
  for (size_t channel = 0; channel < pressed_.size(); channel++) {
    if (pressed_[channel] && state_now - pressed_at_[channel] >= release_timeout_) {
      pressed_[channel] = false;
      ESP_LOGW(TAG, "Button ID %u: release missing for %u ms; marking state unknown",
               static_cast<unsigned>(channel + 1), static_cast<unsigned>(release_timeout_));
      for (auto &entry : sensors_) {
        if (entry.button_id == channel + 1) entry.sensor->invalidate_state();
      }
    }
  }
  if (now - last_summary_ >= 30000) {
    last_summary_ = now;
    ESP_LOGV(TAG, "UART summary: bytes=%u valid_frames=%u rejected=%u pending=%u",
             static_cast<unsigned>(received_), static_cast<unsigned>(valid_),
             static_cast<unsigned>(rejected_), static_cast<unsigned>(parser_.size()));
    if (received_ == 0) ESP_LOGW(TAG, "No UART bytes received yet; check RX=P1, controller power, and pin conflicts");
  }
}

void SonoffT6::dump_config() {
  ESP_LOGCONFIG(TAG, "SONOFF T6 touch UART v1 (receive-only):");
  ESP_LOGCONFIG(TAG, "  Release timeout: %u ms; packet timeout: 200 ms", static_cast<unsigned>(release_timeout_));
  for (auto &entry : sensors_) {
    ESP_LOGCONFIG(TAG, "  Button ID %u:", entry.button_id);
    LOG_BINARY_SENSOR("    ", "Touch", entry.sensor);
  }
}
}  // namespace esphome::sonoff_t6
