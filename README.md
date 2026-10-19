# ESPHome SONOFF T6 UART touch buttons

An ESPHome external component for the SONOFF T6's UART touch controller on
BK7238. **Confirmed working on the owner's two-pad device.**

| Pad | Button ID |
| --- | --- |
| Left | `3` |
| Right | `1` |

The component receives on **P1 at 115200 8N1**. Event `1` means pressed and
event `0` means released. It sends no initialization or settings commands.

## Use

Merge these blocks into your existing working Wi-Fi/API/OTA/relay configuration:

```yaml
external_components:
  - source: github://saltech1/esphome-sonoff-t6-uart@main
    components: [sonoff_t6]

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

Use your existing relay IDs. Use `on_press` instead of `on_release` to toggle
on touch-down. Set `logger.baud_rate: 0`. If retaining UART debugging, set
`uart.debug.dummy_receiver: false` so this component receives the bytes.

See [configuration and diagnostics](docs/sonoff-t6-uart.md) for logging, timeout
behavior and troubleshooting. The [minimal build example](examples/sonoff-t6-uart.yaml)
uses a local component source for development.

## Protocol and diagnostics

The driver validates checksums, handles fragmented/concatenated packets, and
resynchronizes after malformed data. DEBUG logs include raw frames, button
transitions, errors and periodic UART counters. Missing releases mark the
sensor unknown after a configurable timeout.

- [Stock firmware analysis and protocol evidence](docs/sonoff-t6-firmware-analysis.md)
- [Standalone UART capture configuration](examples/sonoff-t6-uart-capture.yaml)

The capture example is a separate diagnostic configuration. Its dummy receiver
must be disabled when using the `sonoff_t6` component.

## Development

Validated with ESPHome 2026.9.1 and LibreTiny 1.13.0:

```sh
python -m unittest discover -s tests
c++ -std=c++17 tests/test_protocol.cpp -o test_protocol
./test_protocol
esphome compile examples/sonoff-t6-uart.yaml
```

The native tests exercise the actual C++ parser against hardware-captured bytes,
noise, invalid checksums/lengths, and fragmented/concatenated frames.
`ci/validate.yml` is a GitHub Actions template; copy it to
`.github/workflows/validate.yml` with a credential that has `workflow` permission
to enable automated CI.

Firmware inspection helpers under `tools/` require Capstone. They operate on
the local dump; firmware images and board photos are excluded from Git.

MIT licensed; see [LICENSE](LICENSE).
