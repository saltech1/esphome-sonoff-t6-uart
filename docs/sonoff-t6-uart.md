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
output:
  - platform: gpio
    pin: P6
    id: relay_right
  - platform: gpio
    pin: P26
    id: relay_left

light:
  - platform: binary
    name: Right Light
    id: right_relay_light
    output: relay_right
  - platform: binary
    name: Left Light
    id: left_relay_light
    output: relay_left

status_led:
  pin:
    number: P9
    inverted: false

external_components:
  - source: github://saltech1/esphome-sonoff-t6-uart@main
    components: [sonoff_t6]

logger:
  baud_rate: 0

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
    on_click:
      - min_length: 10ms
        max_length: 350ms
        then:
          - light.toggle: left_relay_light
  - platform: sonoff_t6
    sonoff_t6_id: touch_controller
    button_id: 1
    name: Right touch
    on_click:
      - min_length: 10ms
        max_length: 350ms
        then:
          - light.toggle: right_relay_light
```

The example uses the owner's pin assignments: right relay P6, left relay P26,
and status LED P9 (not inverted). Each relay is exposed as a binary light.
A tap lasting 10–350 ms toggles its light with `light.toggle`; longer holds do
not match this click automation. Output IDs remain `relay_left`/`relay_right`;
light entity IDs are `left_relay_light`/`right_relay_light`.

The UART framing is 115200 8N1, checked during validation. The driver never
transmits, so `tx_pin` is optional; keeping `tx_pin: P0` is fine. No automatic
handshake or settings commands are sent. Only the two configured button IDs
are exposed; there is no third sensor in the example.

`examples/sonoff-t6-uart.yaml` includes the BK7238 board configuration and the
complete binary-light example. Retain your own Wi-Fi/API/OTA settings when
merging it into your device configuration.

## Diagnostics and behavior

To enable raw packet and summary diagnostics, rebuild with:

```yaml
logger:
  baud_rate: 0
  level: VERBOSE
  logs:
    sonoff_t6: VERBOSE
```


At DEBUG level the driver logs decoded transitions and unknown command/event
codes. Raw frames are logged at VERBOSE level, along with a summary every 30
seconds of received bytes, valid/rejected frames and buffered partial bytes. No received data after 30
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
