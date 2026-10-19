# SONOFF T6 touch firmware findings

Analysis of `SONOFF_T6-3C-120M_BK7238_v1.0.3_decrypted_decompressed.bin`.
SHA-256: `28967f3075b3be6bd2c1033b5939e908a1c653cf9510861cc4f86e59ded28ffc`.
Addresses below use the image's apparent `0x10000` mapping; subtract `0x10000`
to obtain file offsets. The dump and board photo remain local.

## Conclusions and limits

**Hardware follow-up:** the owner's two-pad device subsequently matched twelve
predicted frames (three left taps, then three right taps), with valid checksums.
Left reports button ID 3, right reports ID 1. Each tap reports event 1 then event
0. No handshake/settings button was pressed. The `sonoff_t6` component implements
this receive path; see [usage](sonoff-t6-uart.md).

The application's active touch path uses **UART2, 115200 baud, 8 data bits,
no parity, one stop bit**, on **BK7238 P0/TX and P1/RX**. It sends and receives
checksummed `AA 55` frames. This is strong evidence that an MPR031/032 I2C driver
does not implement this board's stock touch interface.

This establishes the firmware's intended interface. It does not independently
prove electrical continuity on a particular physical board. With the reported
P0/P1 wiring, the probable interpretation is that J2's `SCL` pad carries MCU TX
and its `SDA` pad carries MCU RX; a pad label does not establish the protocol.
Power-off continuity or a logic-analyzer capture would confirm that physical
mapping. The photo alone cannot resolve traces crossing vias/layers.

U2's manufacturer and part number remain unknown. A small programmed touch MCU
or custom touch controller is consistent with the package and protocol, but
that is an inference. No chip-ID/name has been recovered from this path. It is
also not proven solely by static analysis that the UART endpoint is U2 rather
than some other on-board logic. The visible three electrode traces toward U2
and the reported wiring make U2 the leading candidate.

## Evidence chain

| Address | Observation | Implication |
| --- | --- | --- |
| `0x2436E` | Main application calls `0x28D8C` | This is an active application path, not just unused strings |
| `0x28D9C`–`0x28DAE` | Builds a UART config with port `1`, baud `0xE1 << 9`, and other fields zeroed | Port 1, 115200 baud |
| `0x28DDE` | Calls configuration wrapper `0x37518` | Touch UART initialization |
| `0x37518`–`0x37538` | Copies baud to a config with data-length encoding `3`; zero parity/stop fields; calls `0x120E60` | UART framing is 8N1 via hardware config routine `0x18630` |
| `0x120E84`–`0x120EAA` | Nonzero port selects device string `uart2` at `0x12A956` and control request `0x0C124004` | UART2 device selected |
| `0x18B46`–`0x18B4E` | UART2 setup calls GPIO control `0x0CAA0002` with mode `0` | Selects UART2 pin mux |
| `0x1A456`–`0x1A4C6` | Mode-0 mux tables select GPIO range 0 through 1, function 0 | Firmware itself selects P0/P1 |
| `0x28DE4`–`0x28DE8` | Sets port `1` baud explicitly to 115200 again | Confirms the touch baud |
| `0x28E0A`–`0x28E10` | Sends six bytes from RAM `0x4028B0` through `0x375BE` on port 1 | Startup command |
| `0x375D4` → `0x184B0` | Byte transmitter selects registers `0x802208` / `0x80220C` for nonzero port | Direct UART2 FIFO access, not I2C emulation |
| `0x28DFE` / `0x28E02` | Creates `touch_task` at `0x28B55` (Thumb pointer) | Connects UART to the touch parser |
| `0x28B66`–`0x28B6E` | Receives through port 1 / `0x375DC` | Incoming touch data uses the same UART |

The mux tables' entry zero is:

| Table | Address | Value |
| --- | --- | --- |
| Peripheral function | `0x12AA76` | `0` |
| End GPIO | `0x12AAC4` | `1` |
| Start GPIO | `0x12AAEB` | `0` |
| GPIO configuration | `0x12AB12` | `5` |

Pin directions are corroborated by the installed LibreTiny 1.13.0
`boards/variants/generic-bk7238.h`: `PIN_SERIAL2_TX = 0`,
`PIN_SERIAL2_RX = 1`. The Beken SDK uses `UART2_PORT = 1`, base `0x802200`,
and maps `GFUNC_MODE_UART2` to GPIO0/1. These corroborate the dump's own mux
selection; they are not a substitute for tracing that selection.

## Startup and settings commands

The ARM startup copy at `0x1022C` copies initialized data from flash
`0x158E34` to RAM `0x4020E0`, ending at RAM `0x4037B4`. Thus runtime address
`0x4028B0` maps to flash `0x159604` (file offset `0x149604`). Its six bytes are:

```text
AA 55 01 01 00 01
```

The active touch initializer sends exactly these six bytes. It appears to be an
initialization/query/handshake command; its precise name is not encoded here.

In the receive parser, command `0x81`, length `2`, payload `01 00` causes a
12-byte reply to be sent (branch `0x28BD8`–`0x28BF4`). The reply is stored at
flash address `0x12E645`:

```text
AA 55 01 03 06 03 52 03 84 03 52 3A
```

This is command `0x03` with six payload bytes. Interpreting those as three
big-endian 16-bit numbers gives **850, 900, 850**. Per-pad configuration or
sensitivity settings are plausible, but their meaning and units are **not
established**. They are not documented MPR03x register writes. Do not tune them
as if they were MPR03x thresholds.

## Packet structure and events

The active parser at `0x28B54` validates:

```text
AA 55 01 COMMAND LENGTH PAYLOAD... CHECKSUM
CHECKSUM = sum(all preceding frame bytes) & 0xFF
```

It accepts checksum positions corresponding to one-byte or two-byte payloads.
It requires byte 2 to equal `01`. There is no evidence here for an arbitrary
maximum payload size or general framing implementation on the peer; a new
driver should use bounded buffers and validate lengths/checksums independently.

For command `0x02` and length `2`, payload byte 0 is a button identifier
`1`, `2`, or `3`; payload byte 1 is an event code. The code handles:

| Event code | Stock application behavior |
| --- | --- |
| `0` | Toggles the corresponding logical relay, subject to mode/lock checks |
| `1` | Ignored in the touch-task handler |
| `2` | Routes toward provisioning/reset-related handling with state checks |

Do not yet label these as physical press/release/hold phases: that requires
observing a real tap and hold. The relay toggle may occur on release or after
gesture classification. The second event path also contains some broader
command fall-through behavior; a new driver should initially only treat
validated command `0x02` as a button event.

**Predicted, not captured** examples for event zero:

```text
Button 1: AA 55 01 02 02 01 00 05
Button 2: AA 55 01 02 02 02 00 06
Button 3: AA 55 01 02 02 03 00 07
```

Command `0x82`, length `1`, payload `00` / `01` updates an internal flag.
Its exact purpose is unresolved. An earlier `touch_task` at `0x26604` parses
a similar protocol, but the main application call traced above selects the
later handler at `0x28B54`; the earlier function alone should not define this
device's behavior.

## Next hardware capture

Use `examples/sonoff-t6-uart-capture.yaml` by merging its logger/UART/button
blocks into the existing working firmware. Remove the `mpr03x` block and its
binary sensors while testing; both interfaces cannot own P0/P1 simultaneously.
The example logs RX/TX hex and sends nothing automatically. Its optional
template buttons let you send the exact stock startup/settings commands while
the network log viewer is already attached.

1. Start logging, power-cycle once, and tap each pad separately. Record physical
   order (left, center, right) and timestamps. A warm MCU reset may leave the
   other controller configured; a full power cycle tests startup requirements.
2. If no data appears, press **Touch UART stock handshake** once, then repeat
   the pad test. Save any `0x81` response.
3. If that yields the expected reply but no button events, press **Touch UART
   stock settings** once, then repeat the test. This debugger does not send the
   stock reply automatically.
4. Include one short tap and one several-second hold per pad. Paste the full hex
   log even if checksum validation is not yet implemented.

Silence does not disprove UART: controller power/reset, physical connections,
startup sequence, and firmware/board revision matching remain possible causes.
An ACK-less MPR03x test would not by itself distinguish those causes.

## Reproduction

Install Capstone and run `python tools/touch_firmware_report.py`. It checks the
known dump hash, call instructions, mux table, startup data mapping and command
checksums without printing unrelated firmware contents. For inspection:

```text
python tools/disassemble.py 0x24300 0x24376
python tools/disassemble.py 0x28D8C 0x28E46
python tools/disassemble.py 0x28B54 0x28D6E
python tools/disassemble.py 0x1022C 0x1024C --arm
python tools/firmware_xrefs.py refs 0x28d8c 0x37518 0x184b0
```

The disassembly helpers are inspection aids, not a complete disassembler or a
proof of reachability for every decoded instruction. The report anchors the
critical checks to this exact image.

References for corroborating platform definitions:

- [LibreTiny BK7238 pin definitions, v1.13.0](https://github.com/libretiny-eu/libretiny/blob/v1.13.0/boards/variants/generic-bk7238.h)
- [Beken GPIO driver at the installed SDK revision](https://github.com/libretiny-eu/framework-beken-bdk/blob/44800e7451ea30fbcbd3bb6e905315de59349fee/beken378/driver/gpio/gpio.c)
- [ESPHome UART debugging](https://esphome.io/components/uart/#debugging)
