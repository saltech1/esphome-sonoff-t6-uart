#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "protocol.h"
#include <vector>

namespace esphome::sonoff_t6 {
class TouchBinarySensor : public binary_sensor::BinarySensor {};

class SonoffT6 : public Component, public uart::UARTDevice {
 public:
  void set_release_timeout(uint32_t timeout) { release_timeout_ = timeout; }
  void register_sensor(TouchBinarySensor *sensor, uint8_t button_id) { sensors_.push_back({sensor, button_id}); }
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

 protected:
  struct Sensor { TouchBinarySensor *sensor; uint8_t button_id; };
  std::vector<Sensor> sensors_;
  PacketParser parser_;
  std::array<bool, 3> pressed_{};
  std::array<uint32_t, 3> pressed_at_{};
  uint32_t release_timeout_{30000};
  uint32_t last_byte_{0};
  uint32_t last_summary_{0};
  uint32_t received_{0};
  uint32_t valid_{0};
  uint32_t rejected_{0};
  void handle_frame_(const Frame &frame);
};
}  // namespace esphome::sonoff_t6
