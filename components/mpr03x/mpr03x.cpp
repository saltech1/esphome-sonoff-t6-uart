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
    if (micros() - start >= 1000) {
      if (error_ == nullptr) error_ = "SCL stayed LOW for 1000 us";
      return false;
    }
    delayMicroseconds(1);
  }
  pause_();
  return true;
}

bool MPR03X::start_() {
  release_(sda_);
  pause_();
  if (!clock_high_()) return false;
  if (!sda_->digital_read()) {
    if (error_ == nullptr) error_ = "SDA LOW before START";
    return false;
  }
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
  if (!ack && error_ == nullptr) error_ = "NACK";
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
  error_ = nullptr;
  phase_ = "START";
  bool ok = start_();
  if (ok) { phase_ = "write address"; ok = write_byte_(address_ << 1); }
  if (ok) { phase_ = "register pointer"; ok = write_byte_(reg); }
  if (ok) { phase_ = "register value"; ok = write_byte_(value); }
  if (ok) phase_ = "STOP";
  const bool stopped = stop_();
  if (!ok || !stopped) log_transfer_failure_("write", reg);
  else ESP_LOGVV(TAG, "Write reg=0x%02X value=0x%02X OK", reg, value);
  return ok && stopped;
}

bool MPR03X::read_register_(uint8_t reg, uint8_t &value) {
  error_ = nullptr;
  phase_ = "START";
  bool ok = start_();
  if (ok) { phase_ = "write address"; ok = write_byte_(address_ << 1); }
  if (ok) { phase_ = "register pointer"; ok = write_byte_(reg); }
  if (ok) { phase_ = "repeated START"; ok = start_(); }
  if (ok) { phase_ = "read address"; ok = write_byte_((address_ << 1) | 1); }
  if (ok) { phase_ = "read data/final NACK"; ok = read_byte_(value); }
  if (ok) phase_ = "STOP";
  const bool stopped = stop_();
  if (!ok || !stopped) log_transfer_failure_("read", reg);
  else ESP_LOGVV(TAG, "Read reg=0x%02X value=0x%02X OK", reg, value);
  return ok && stopped;
}

void MPR03X::log_transfer_failure_(const char *operation, uint8_t reg) {
  // Log only after STOP, never in a clock/data timing window.
  ESP_LOGW(TAG, "I2C %s addr=0x%02X reg=0x%02X phase=%s: %s; released SCL=%s SDA=%s", operation,
           address_, reg, phase_, error_ == nullptr ? "bus did not release after STOP" : error_,
           scl_->digital_read() ? "HIGH" : "LOW", sda_->digital_read() ? "HIGH" : "LOW");
}

void MPR03X::probe_addresses_() {
  // Diagnostic scan is address-only: no register pointers or data are written.
  // Reserved I2C addresses are deliberately excluded.
  uint8_t responders = 0;
  const uint8_t begin = diagnostics_ ? 0x08 : 0x4A;
  const uint8_t end = diagnostics_ ? 0x77 : 0x4B;
  for (uint8_t address = begin; address <= end; address++) {
    error_ = nullptr;
    const bool ack = start_() && write_byte_(address << 1);
    const bool stopped = stop_();
    if (ack) responders++;
    if (ack || address == 0x4A || address == 0x4B || !stopped) {
      ESP_LOGI(TAG, "Address probe 0x%02X: %s (STOP=%s)", address,
               ack ? "ACK" : (error_ == nullptr ? "no response" : error_), stopped ? "OK" : "FAILED");
    }
    if (!stopped) {
      ESP_LOGW(TAG, "Address scan aborted: bus did not return idle");
      break;
    }
  }
  ESP_LOGI(TAG, "Address scan 0x%02X..0x%02X: %u responder(s)", begin, end, responders);
  ESP_LOGI(TAG, "Probe ACK is not a chip identity check; configured address remains 0x%02X", address_);
}

bool MPR03X::write_checked_(uint8_t reg, uint8_t value) {
  if (!write_register_(reg, value)) return false;
  uint8_t actual;
  if (!read_register_(reg, actual)) return false;
  ESP_LOGD(TAG, "Configuration reg=0x%02X expected=0x%02X readback=0x%02X %s", reg, value, actual,
           value == actual ? "OK" : "MISMATCH");
  if (value != actual) {
    ESP_LOGW(TAG, "Register 0x%02X mismatch: wrote 0x%02X, read 0x%02X; verify controller identity", reg, value, actual);
    return false;
  }
  return true;
}

bool MPR03X::configure_() {
  error_ = nullptr;
  ESP_LOGI(TAG, "Initialization attempt %u; released bus SCL=%s SDA=%s", static_cast<unsigned>(++attempts_),
           scl_->digital_read() ? "HIGH" : "LOW", sda_->digital_read() ? "HIGH" : "LOW");
  if (!recover_()) {
    ESP_LOGW(TAG, "Bus recovery failed (up to 9 clocks): %s; SCL=%s SDA=%s",
             error_ == nullptr ? "SDA did not release" : error_,
             scl_->digital_read() ? "HIGH" : "LOW", sda_->digital_read() ? "HIGH" : "LOW");
    return false;
  }
  if (probe_pending_) { probe_addresses_(); probe_pending_ = false; }
  uint8_t initial_status, initial_config;
  if (!read_register_(0x00, initial_status) || !read_register_(ELECTRODE_CONFIG, initial_config)) return false;
  ESP_LOGI(TAG, "Before configuration: status=0x%02X electrode_config=0x%02X", initial_status, initial_config);
  if (!write_checked_(ELECTRODE_CONFIG, 0)) return false;
  // MPR03x registers; these are NOT the MPR121 filter/ECR addresses.
  if (!write_checked_(0x26, 0x01) || !write_checked_(0x27, 0x01) ||
      !write_checked_(0x28, 0x00)) return false;
  for (uint8_t channel = 0; channel < 3; channel++) {
    if (!write_checked_(0x29 + 2 * channel, touch_threshold_) ||
        !write_checked_(0x2A + 2 * channel, release_threshold_)) return false;
  }
  return write_checked_(0x41, afe_configuration_) && write_checked_(0x43, filter_configuration_) &&
         write_checked_(ELECTRODE_CONFIG, 0x03);
}

bool MPR03X::log_diagnostics_() {
  uint8_t electrode_config;
  if (!read_register_(ELECTRODE_CONFIG, electrode_config)) return false;
  ESP_LOGD(TAG, "Diagnostics uptime=%u ms reads=%u failures=%u electrode_config=0x%02X status=0x%02X",
           static_cast<unsigned>(millis()), static_cast<unsigned>(reads_), static_cast<unsigned>(failures_),
           electrode_config, previous_status_);
  for (uint8_t channel = 0; channel < 3; channel++) {
    uint8_t low, high, baseline;
    if (!read_register_(0x02 + 2 * channel, low) || !read_register_(0x03 + 2 * channel, high) ||
        !read_register_(0x1A + channel, baseline)) return false;
    const unsigned filtered = (static_cast<unsigned>(high & 3) << 8) | low;
    const unsigned reference = static_cast<unsigned>(baseline) << 2;
    ESP_LOGD(TAG, "Electrode %u: filtered=%u baseline=%u delta=%d touch/release=%u/%u touched=%s",
             channel, filtered, reference, static_cast<int>(reference) - static_cast<int>(filtered),
             touch_threshold_, release_threshold_, (previous_status_ & (1U << channel)) ? "YES" : "NO");
  }
  // Detect a controller reset even if its status register still ACKs.
  if (electrode_config != 0x03) {
    ESP_LOGW(TAG, "Electrode config changed to 0x%02X (expected 0x03); reinitializing", electrode_config);
    return false;
  }
  return true;
}

void MPR03X::setup() {
  scl_->setup();
  sda_->setup();
  release_(scl_);
  release_(sda_);
  ESP_LOGI(TAG, "MPR03x driver diagnostics revision 2; diagnostics=%s; internal_pullups=%s",
           diagnostics_ ? "YES (1s snapshots)" : "NO", internal_pullups_ ? "YES" : "NO");
  // Also covers a controller that powers up slightly after the MCU.
  last_attempt_ = millis() - 2000;
}

void MPR03X::communication_failed_() {
  status_set_warning();
  configured_ = false;
  ready_ = false;
  have_status_ = false;
  failures_++;
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
    last_diagnostics_ = millis() - 1000;
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
  reads_++;
  if (!have_status_ || status != previous_status_) {
    ESP_LOGD(TAG, "Touch status=0x%02X E0=%s E1=%s E2=%s", status,
             status & 1 ? "ON" : "OFF", status & 2 ? "ON" : "OFF", status & 4 ? "ON" : "OFF");
    previous_status_ = status;
    have_status_ = true;
  }
  for (auto &entry : sensors_) entry.sensor->publish_state((status & (1U << entry.channel)) != 0);
  if (diagnostics_ && millis() - last_diagnostics_ >= 1000) {
    last_diagnostics_ = millis();
    if (!log_diagnostics_()) communication_failed_();
  }
}

void MPR03X::dump_config() {
  ESP_LOGCONFIG(TAG, "MPR03x software I2C:");
  ESP_LOGCONFIG(TAG, "  Diagnostics revision: 2; raw snapshots: %s; internal pull-ups: %s",
                diagnostics_ ? "YES" : "NO", internal_pullups_ ? "YES" : "NO");
  LOG_PIN("  SCL: ", scl_);
  LOG_PIN("  SDA: ", sda_);
  ESP_LOGCONFIG(TAG, "  Address: 0x%02X, touch/release: %u/%u", address_, touch_threshold_, release_threshold_);
  ESP_LOGCONFIG(TAG, "  AFE: 0x%02X, filter: 0x%02X, half period: %u us", afe_configuration_, filter_configuration_,
                static_cast<unsigned>(half_period_us_));
  LOG_UPDATE_INTERVAL(this);
}
}  // namespace esphome::mpr03x
