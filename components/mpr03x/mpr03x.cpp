#include "mpr03x.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::mpr03x {
static const char *const TAG = "mpr03x";
static constexpr uint8_t ELECTRODE_CONFIG = 0x44;

void MPR03X::pause_() { delayMicroseconds(half_period_us_); }

void MPR03X::release_(InternalGPIOPin *pin) {
  if (internal_pullups_) {
    pin->pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP);
  } else {
    pin->pin_mode(gpio::FLAG_INPUT);
  }
}

void MPR03X::low_(InternalGPIOPin *pin) {
  // Never drive a high level, including while changing direction.
  pin->digital_write(false);
  pin->pin_mode(gpio::FLAG_OUTPUT);
  pin->digital_write(false);
}

bool MPR03X::clock_high_() {
  release_(scl_);
  const uint32_t start = micros();
  while (!scl_->digital_read()) {
    if (micros() - start >= 1000) return false;
    delayMicroseconds(1);
  }
  pause_();
  return true;
}

bool MPR03X::start_() {
  release_(sda_);
  pause_();
  if (!clock_high_() || !sda_->digital_read()) return false;
  low_(sda_);
  pause_();
  low_(scl_);
  pause_();
  return true;
}

bool MPR03X::stop_() {
  low_(scl_);
  low_(sda_);
  pause_();
  const bool clock_ok = clock_high_();
  release_(sda_);
  pause_();
  return clock_ok && sda_->digital_read();
}

bool MPR03X::recover_() {
  release_(sda_);
  if (!clock_high_()) return false;
  // A reset in the middle of a read can leave a slave driving SDA low.
  for (uint8_t i = 0; i < 9 && !sda_->digital_read(); i++) {
    low_(scl_);
    pause_();
    if (!clock_high_()) return false;
  }
  return stop_();
}

bool MPR03X::write_byte_(uint8_t value) {
  for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
    if (value & mask) release_(sda_); else low_(sda_);
    pause_();
    if (!clock_high_()) return false;
    low_(scl_);
    pause_();
  }
  release_(sda_);
  pause_();
  if (!clock_high_()) return false;
  const bool ack = !sda_->digital_read();
  low_(scl_);
  pause_();
  return ack;
}

bool MPR03X::read_byte_(uint8_t &value) {
  value = 0;
  release_(sda_);
  for (uint8_t i = 0; i < 8; i++) {
    pause_();
    if (!clock_high_()) return false;
    value = (value << 1) | (sda_->digital_read() ? 1 : 0);
    low_(scl_);
    pause_();
  }
  // Single-byte read: NACK the final byte.
  release_(sda_);
  pause_();
  if (!clock_high_()) return false;
  low_(scl_);
  pause_();
  return true;
}

bool MPR03X::write_register_(uint8_t reg, uint8_t value) {
  const bool ok = start_() && write_byte_(address_ << 1) && write_byte_(reg) && write_byte_(value);
  const bool stopped = stop_();
  return ok && stopped;
}

bool MPR03X::read_register_(uint8_t reg, uint8_t &value) {
  const bool ok = start_() && write_byte_(address_ << 1) && write_byte_(reg) &&
                  start_() && write_byte_((address_ << 1) | 1) && read_byte_(value);
  const bool stopped = stop_();
  return ok && stopped;
}

bool MPR03X::configure_() {
  if (!recover_() || !write_register_(ELECTRODE_CONFIG, 0)) return false;
  // MPR03x registers; these are NOT the MPR121 filter/ECR addresses.
  if (!write_register_(0x26, 0x01) || !write_register_(0x27, 0x01) ||
      !write_register_(0x28, 0x00)) return false;
  for (uint8_t channel = 0; channel < 3; channel++) {
    if (!write_register_(0x29 + 2 * channel, touch_threshold_) ||
        !write_register_(0x2A + 2 * channel, release_threshold_)) return false;
  }
  if (!write_register_(0x41, afe_configuration_) || !write_register_(0x43, filter_configuration_) ||
      !write_register_(ELECTRODE_CONFIG, 0x03)) return false;
  uint8_t value;
  return read_register_(ELECTRODE_CONFIG, value) && value == 0x03;
}

void MPR03X::setup() {
  scl_->setup();
  sda_->setup();
  release_(scl_);
  release_(sda_);
  // Also covers a controller that powers up slightly after the MCU.
  last_attempt_ = millis() - 2000;
}

void MPR03X::communication_failed_() {
  status_set_warning();
  configured_ = false;
  ready_ = false;
  last_attempt_ = millis();
  // Mark states unknown; a bus failure must not create a press/release event.
  for (auto &entry : sensors_) entry.sensor->invalidate_state();
}

void MPR03X::update() {
  if (!configured_) {
    if (millis() - last_attempt_ < 2000) return;
    last_attempt_ = millis();
    if (!configure_()) {
      ESP_LOGW(TAG, "No response/configuration failed at 0x%02X; check SDA/SCL and pull-ups", address_);
      communication_failed_();
      return;
    }
    configured_ = true;
    ready_ = false;
    // Give filtering and initial baseline acquisition time before publishing.
    last_attempt_ = millis();
    ESP_LOGI(TAG, "Configured three electrodes at 0x%02X", address_);
    return;
  }
  if (!ready_) {
    if (millis() - last_attempt_ < 500) return;
    ready_ = true;
  }
  uint8_t status;
  if (!read_register_(0x00, status)) {
    ESP_LOGW(TAG, "Touch status read failed; retrying initialization");
    communication_failed_();
    return;
  }
  if (status & 0x80) {
    ESP_LOGE(TAG, "Controller overcurrent flag set; check REXT/controller supply");
    communication_failed_();
    return;
  }
  status_clear_warning();
  for (auto &entry : sensors_) entry.sensor->publish_state((status & (1U << entry.channel)) != 0);
}

void MPR03X::dump_config() {
  ESP_LOGCONFIG(TAG, "MPR03x software I2C:");
  LOG_PIN("  SCL: ", scl_);
  LOG_PIN("  SDA: ", sda_);
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X, touch/release: %u/%u", address_, touch_threshold_, release_threshold_);
  ESP_LOGCONFIG(TAG, "  AFE: 0x%02X, filter: 0x%02X, half period: %u us", afe_configuration_, filter_configuration_,
                static_cast<unsigned>(half_period_us_));
  LOG_UPDATE_INTERVAL(this);
}
}  // namespace esphome::mpr03x
