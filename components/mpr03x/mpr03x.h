#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include <vector>

namespace esphome::mpr03x {

class MPR03XBinarySensor : public binary_sensor::BinarySensor {};

class MPR03X : public PollingComponent {
 public:
  void set_scl_pin(InternalGPIOPin *pin) { scl_ = pin; }
  void set_sda_pin(InternalGPIOPin *pin) { sda_ = pin; }
  void set_address(uint8_t value) { address_ = value; }
  void set_touch_threshold(uint8_t value) { touch_threshold_ = value; }
  void set_release_threshold(uint8_t value) { release_threshold_ = value; }
  void set_afe_configuration(uint8_t value) { afe_configuration_ = value; }
  void set_filter_configuration(uint8_t value) { filter_configuration_ = value; }
  void set_half_period_us(uint32_t value) { half_period_us_ = value; }
  void set_internal_pullups(bool value) { internal_pullups_ = value; }
  void set_diagnostics(bool value) { diagnostics_ = value; }
  void register_sensor(MPR03XBinarySensor *sensor, uint8_t channel) { sensors_.push_back({sensor, channel}); }
  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  struct Sensor { MPR03XBinarySensor *sensor; uint8_t channel; };
  InternalGPIOPin *scl_{nullptr};
  InternalGPIOPin *sda_{nullptr};
  std::vector<Sensor> sensors_;
  uint8_t address_{0x4A};
  uint8_t touch_threshold_{12};
  uint8_t release_threshold_{6};
  uint8_t afe_configuration_{0x10};
  uint8_t filter_configuration_{0x24};
  uint32_t half_period_us_{10};
  bool internal_pullups_{false};
  bool configured_{false};
  bool ready_{false};
  uint32_t last_attempt_{0};
  bool diagnostics_{false};
  bool probe_pending_{true};
  bool have_status_{false};
  uint8_t previous_status_{0};
  uint32_t last_diagnostics_{0};
  uint32_t attempts_{0};
  uint32_t failures_{0};
  uint32_t reads_{0};
  const char *error_{nullptr};
  const char *phase_{"idle"};
  void pause_();
  void release_(InternalGPIOPin *pin);
  void low_(InternalGPIOPin *pin);
  bool clock_high_();
  bool start_();
  bool stop_();
  bool recover_();
  bool write_byte_(uint8_t value);
  bool read_byte_(uint8_t &value);
  bool write_register_(uint8_t reg, uint8_t value);
  bool read_register_(uint8_t reg, uint8_t &value);
  bool configure_();
  bool write_checked_(uint8_t reg, uint8_t value);
  void probe_addresses_();
  void log_transfer_failure_(const char *operation, uint8_t reg);
  bool log_diagnostics_();
  void communication_failed_();
};

}  // namespace esphome::mpr03x
