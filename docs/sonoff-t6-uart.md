# SONOFF T6 UART touch component

The `sonoff_t6` external component receives the stock touch controller's UART
packets on BK7238. No I2C implementation or U2 part number is needed.

The user's **two-pad device** produced twelve valid packets from three left
taps followed by three right taps. Left reports **button ID 3**, right reports
**button ID 1**. Each tap reports event 1 then event 0, separated by 120–263 ms,
consistent with press and release. These map to ON and OFF, respectively.
No stock handshake/settings command was sent before the packets appeared.

The actual C++ parser is tested against all twelve captured frames. The complete
component compiles for BK7238 and the owner has confirmed it works on the device.

## Configuration

Merge this into the existing working Wi-Fi/API/OTA/relay configuration. Replace
the capture debugger's dummy receiver with this driver: if keeping UART debugging,
**set `dummy_receiver: false`**.
Configuration validation rejects a competing dummy receiver.

```yaml
external_components:
  - source: github://saltech1/esphome-mpr03x@main
    refresh: 0s  # Useful while testing updates; omit for normal operation.
    components: [sonoff_t6]

logger:
  baud_rate: 0
  level: DEBUG
  logs:
    sonoff_t6: DEBUG

uart:
  id: touch_uart
  rx_pin: P1
  baud_rate: 115200

sonoff_t6:
  id: touch_controller
  uart_id: touch_uart

binary_sensor:
  - platform: sonoff_t6
    sonoff_t6_id: touch_controller
    button_id: 3
    name: Left touch
    on_release:
      - switch.toggle: relay_left
  - platform: sonoff_t6
    sonoff_t6_id: touch_controller
    button_id: 1
    name: Right touch
    on_release:
      - switch.toggle: relay_right
```

Use your existing relay IDs. First verify sensor mapping before attaching relay
actions. `on_release` follows the stock firmware's event-0 toggle behavior;
use `on_press` instead if you want the action on event 1. Do not configure both
to toggle the same relay.

The UART framing is 115200 8N1, checked during validation. The driver never
transmits, so `tx_pin` is optional; keeping `tx_pin: P0` is fine. No automatic
handshake or settings commands are sent. Only the two configured button IDs
are exposed; there is no third sensor in the example.

`examples/sonoff-t6-uart.yaml` is a minimal compile example, not a replacement
for your complete working device configuration.

## Diagnostics and behavior

At DEBUG level the driver logs valid raw frames, decoded transitions, and
unknown command/event codes. Every 30 seconds it reports received bytes,
valid/rejected frames and buffered partial bytes. No received data after 30
seconds produces a warning. Invalid version, length, checksum and timed-out
partial packets are logged separately.

The parser accepts fragmented/concatenated frames, skips noise, checks the
additive checksum and resynchronizes after errors. Payloads are bounded to 32
bytes. Partial frames expire after 200 ms. Only command `0x02`, length `2`,
valid button IDs and event codes `0/1` update states. Unknown events, including
code 2, are logged without triggering stock provisioning/reset behavior.

Sensors start OFF. Repeated identical states do not retrigger normal binary
sensor automations. If a pressed state has no release for 30 seconds, the driver
logs the missing release and marks it unknown without inventing a release
event. The optional `release_timeout` setting accepts at least 1 second; increase
it for unusually long holds. Normal idle silence cannot establish whether the
controller is disconnected because no heartbeat has been observed.

Next hardware checks: both pads, one long hold, and a full power cycle. Long-hold
and simultaneous-touch packets remain unconfirmed. If cold boot needs extra
initialization, the separate UART capture example retains the exact recovered
stock commands for investigation.

## Development

```sh
python -m unittest discover -s tests
c++ -std=c++17 tests/test_protocol.cpp -o test_protocol
./test_protocol
esphome compile examples/sonoff-t6-uart.yaml
```

The native tests execute the actual `protocol.h` parser using hardware-captured
bytes and malformed/fragmented/concatenated streams. Any native C++17 compiler
works. Windows verification used `python -m ziglang c++` with an executable in
`.cache/`. The GitHub Actions template in `ci/` includes these tests; active CI
is not enabled with the current publishing credential's permissions.
