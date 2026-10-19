# ESPHome MPR03x touch buttons

External component for the MPR031/MPR032 capacitive touch controller, targeting
the SONOFF T6 with a BK7238 and **SCL on P0 / SDA on P1**. Implements a private
software I2C bus through ESPHome GPIO APIs; it does not depend on `Wire`, the
ESPHome `i2c:` component, or LibreTiny hardware I2C support.

**Status: experimental, not yet tested on physical hardware.** The supplied
example targets BK7238. Controller identity, bus address, physical electrode
order, and sensitivity still need confirmation on the actual switch.

## Use with your working configuration

Keep your existing board, network, relays and LEDs. Copy the `components/mpr03x`
directory beside your YAML under `components/`, then add:

```yaml
external_components:
  - source:
      type: local
      path: components
    components: [mpr03x]

mpr03x:
  id: touch_controller
  scl: P0
  sda: P1
  address: 0x4A
  touch_threshold: 12
  release_threshold: 6
  update_interval: 20ms

binary_sensor:
  - platform: mpr03x
    mpr03x_id: touch_controller
    channel: 0
    name: Touch electrode 0
    on_press:
      - switch.toggle: relay_1  # Your existing relay ID
  - platform: mpr03x
    mpr03x_id: touch_controller
    channel: 1
    name: Touch electrode 1
    on_press:
      - switch.toggle: relay_2
  - platform: mpr03x
    mpr03x_id: touch_controller
    channel: 2
    name: Touch electrode 2
    on_press:
      - switch.toggle: relay_3
```

Verify electrode order in logs before attaching the relay automations. Use
`logger: {baud_rate: 0}` if UART logging otherwise claims the bus pins. Don't
assign P0/P1 to another component, including `i2c:` or UART.

To load the component from GitHub, replace the local source with:

```yaml
external_components:
  - source: github://saltech1/esphome-mpr03x@main
    components: [mpr03x]
```

`examples/sonoff-t6-touch.yaml` is a minimal compile example; merge its touch
blocks into your existing firmware configuration for normal use.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `scl` / `sda` | `P0` / `P1` | Internal GPIO pins, non-inverted |
| `address` | `0x4A` | MPR031; select `0x4B` for MPR032 |
| `touch_threshold` | `12` | 1–255; lower values increase sensitivity |
| `release_threshold` | `6` | 0–254; must be below touch threshold |
| `afe_configuration` | `0x10` | Raw MPR03x AFE register `0x41` |
| `filter_configuration` | `0x24` | Raw filter register `0x43` |
| `half_period_us` | `10` | 5–100 µs; GPIO overhead makes the clock slower |
| `internal_pullups` | `false` | Optional MCU pull-ups; prefer existing external pull-ups |
| `update_interval` | `20ms` | Button polling interval, minimum 10 ms |

The initialization values are starting values, **not recovered SONOFF factory
settings**. All three electrodes are enabled in independent Run1 mode with
baseline tracking. Configuration is written in Stop mode. No IRQ pin is used,
because IRQ and electrode 2 share a pin on this controller.

The bus only drives lines low and releases them by switching to input. It checks
ACKs, uses repeated START for register reads, NACKs the final read byte, bounds
clock waits to 1 ms, and attempts up to nine recovery clocks during initialization.
Communication failures invalidate button states and retry initialization after
two seconds. The first readings are delayed 500 ms after initialization for
baseline/filter settling. Overcurrent is reported as an error.

## Hardware troubleshooting

1. Look for `Configured three electrodes at 0x4A` in the logs. If initialization
   fails, verify the chip marking and try `address: 0x4B` for an MPR032.
2. Check that both lines idle high. The reported 1 kΩ **series** resistors are
   not pull-up resistors. Confirm the existing board's pull-ups and voltage rail;
   do not assume the controller is powered at the MCU voltage.
3. If the bus responds but touches do not register, adjust thresholds first,
   then the AFE/filter values using the datasheet. Keep fingers off the pads
   during boot and baseline acquisition.
4. Verify every channel's press and release, simultaneous touches, a long hold,
   MCU restart, and a complete power cycle before using the component normally.

## Firmware inspection

The supplied decrypted/decompressed dump stays local and is excluded from Git.
`tools/inspect_firmware.py` lists touch-related strings;
`tools/disassemble.py START END` provides local Thumb disassembly using Capstone
and the dump's apparent `0x10000` mapping.

The two references found to `touch_task` lead to functions at `0x26604` and
`0x28B54` that consume buffers containing `AA 55` headers and checksums. They
appear to handle a serial touch protocol. This observation **does not establish
the physical interface of this particular board**, nor exclude another MPR03x
code path. MPR03x initialization has not been identified in the dump. The driver
is based on the reported wiring and public register documentation.

## Development and publishing

Configuration tests and the complete BK7238 firmware build pass against
ESPHome 2026.9.1 and LibreTiny 1.13.0. Physical touch behavior remains untested.
Install ESPHome, then:

```powershell
esphome config examples/sonoff-t6-touch.yaml
esphome compile examples/sonoff-t6-touch.yaml
python -m unittest discover -s tests
```

`ci/validate.yml` is a GitHub Actions template. To enable it, copy it to
`.github/workflows/validate.yml` using a GitHub login with `workflow` permission.
The publishing credential did not have that permission, so automatic CI is not
enabled in this repository.

## References

- [NXP/Freescale MPR03x datasheet](https://www.nxp.com.cn/docs/en/data-sheet/MPR03X.pdf)
- [ESPHome LibreTiny GPIO implementation](https://github.com/esphome/esphome/blob/2026.9.1/esphome/components/libretiny/gpio_arduino.cpp)
- [ESPHome external components](https://esphome.io/components/external_components/)

The MPR03x register map differs from the MPR121; MPR121 initialization sequences
must not be used with this component.
