# How it was reverse engineered

Tools used: a cheap multimeter, an ESP32-S3 board (a conference badge), a breadboard,
a handful of resistors, a BC337 transistor and the Arduino IDE. No oscilloscope, no
logic analyzer.

## 1. Looking at the board

The controller (silkscreen KR-026-R1) has:

- `VIN` / `GND` pads: 13 V DC from a plug-in adapter.
- `L+`, `L1-`, `L2-` pads to the curtain. On this unit only `L+` and `L1-` have wires.
- an IR receiver, a microphone, a push button, a 24 MHz crystal, a 16-pin MCU, a PCB-trace
  antenna (presumably Bluetooth), and a step-down converter (inductor marked 4R7, Schottky
  diode SS24) that makes the LED supply.
- a small transistor stage (Q1 and two diodes) between the MCU and the output pads.

`L1-` / `L2-` with a common `L+` looks like a plain two-group fairy light, which is what we
first assumed. Two observations contradicted that: every LED can show its own colour, and
the remote can set one fixed colour, so the LEDs do receive data.

## 2. Multimeter

Resistance, power off, against the `GND` pad:

| Pad | Reading |
|-----|---------|
| `L1-` | 480 Ω in one direction, open in the other (a semiconductor path: the LED chips and the board) |
| `L2-` | open |

Voltages, power on, stock controller showing a fixed colour at full brightness:

| Pad | Reading |
|-----|---------|
| `VIN` | 13 V |
| `L+` | 4.96 V |
| `L1-` | 1.98–2.02 V (a fast signal averaged by the meter) |
| `L2-` | 0 V |

The 2 V average on `L1-` did not change between "lights on" and "lights off" on the
remote, so the controller transmits continuously. Then we noticed only two wires leave
the board, which rules out any WS281x-type protocol.

## 3. ESP32 as a logic analyzer

A tight loop reading the GPIO input register on the ESP32-S3 runs at about 10 million
samples per second (60,000 samples took 6003 µs), good enough for pulses of a few
microseconds. `firmware/pulse_capture` records 60,000 samples with interrupts disabled,
run-length encodes them and prints a histogram of pulse widths.

`L1-` carries up to 5 V, so it went through a divider (2 kΩ to the GPIO, 2 kΩ + 1 kΩ to
ground, ≈3 V at 5 V) with the ESP32 ground tied to the board's `GND` pad.

Result (`docs/captures/pulse_widths.txt`): two clusters of high pulses (2.4–2.5 µs and
4.4–4.5 µs) and two clusters of low pulses (2.7–2.8 µs and 5.9–6.0 µs), always paired
short-with-short and long-with-long, no gaps longer than 50 µs in 6 ms. Not WS281x
(whose bits are 1.25 µs), but clearly two symbol lengths: bits.

## 4. Decoding frames

`firmware/pulse_decode` records up to 80,000 transitions, classifies every high pulse as
`0` (< 3.5 µs) or `1`, splits the stream on low periods longer than 20 µs and prints bytes.
Every frame was 9617 bits, preceded by a 39 µs pause, starting with `00 01`:

| Remote preset | Bytes per LED |
|---------------|---------------|
| red   | `1E 00 00` |
| green | `00 1E 00` |
| blue  | `00 00 1E` |
| white | `0F 0A 0F` |
| blue-purple | `07 00 1E` |

16 header bits + 400 × 24 bits + 1 trailing bit = 9617. Three bytes per LED in R, G, B
order, 30 as the maximum the stock controller uses. Captures are in `docs/captures/`.

## 5. Sending

`firmware/led_sender` generates the waveform by bit-banging a GPIO against the CPU cycle
counter (`ESP.getCycleCount()`, 240 MHz → 4 ns resolution) with interrupts disabled for the
duration of a frame. The GPIO drives a low-side switch in the curtain's `L1-` wire; the
stock board keeps supplying `L+`.

Things that went wrong, in order, so you can skip them:

1. **Transistor pinout.** A BC337 is C-B-E with the flat face towards you (a 2N2222 is
   E-B-C). Reversed, it still conducts but with a gain of about 5, and the collector sat at
   1.5 V instead of 0.2 V. The diode-test function of the multimeter tells base from
   emitter: red probe on the base, the higher of the two readings is the emitter.
2. **Flicker.** The first sender waited 40 ms between frames. The LEDs are a little dimmer
   while data is being received (the capacitor cannot carry the LED fully), so alternating
   55 ms of data with 40 ms of idle gave a 10 Hz brightness wobble. Sending frames back to
   back (1 ms gap) fixed it completely.
3. **Random colours at higher brightness.** With 12 mA of base current a BC337 does not
   saturate at the current that 400 LEDs draw on white: the supply to the LEDs drops during
   the "on" periods and they misread bits. Lowering the values to 10 made it stable; a
   MOSFET is the real fix. The "off" pulse lengths were also stretched slightly (2.7 and
   5.0 µs instead of 2.5 and 4.5) to compensate for the transistor's turn-off delay.
4. **USB input dropped bytes.** The ESP32-S3's default USB CDC receive buffer is 256 bytes;
   a 1200-byte frame arrives in one burst. `Serial.setRxBufferSize(8192)` before
   `Serial.begin()`, plus sending in 240-byte chunks from the PC, fixed it. The sender was
   also moved to core 0 so that core 1 can service USB while a frame is being transmitted.

## 6. Mapping the matrix

A single moving pixel showed the order: down the first strand, then down the next, and so
on. `index = strand * 20 + position_from_top`.

## 7. If you want to repeat this on another unit

1. Count the wires leaving the controller. Three wires → probably WS281x-type, try WLED
   first. Two wires → this protocol family.
2. Measure `L+` to `GND` (supply voltage) and `L1-` to `GND` (around 2 V average means a
   pulsed return line).
3. Capture `L1-` through a divider with `pulse_capture`, then `pulse_decode`. Compare the
   bytes for red, green and blue presets.
4. Drive it with `led_sender`, starting with low values and a transistor pinout you have
   verified with the diode test.
