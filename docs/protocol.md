# Protocol description

Unit documented: Action LED curtain, 400 RGB LEDs in 20 hanging strands of 20, controller
board silkscreen **KR-026-R1**, two wires between controller and curtain. All numbers below
were measured on this unit with the stock controller running; see
`reverse-engineering.md` for how.

## 1. Physical layer

- Two wires: `L+` and `L1-`.
- `L+` carries the LED supply. The controller board makes it from the 13 V adapter; with
  the stock controller it measured **4.96 V** at full-brightness white.
- `L1-` is the return line. On the board it goes to a low-side transistor to ground.
  Current flows → LEDs are powered. Transistor off → the line floats up towards `L+`
  and the LEDs are without supply for the duration of the pulse.
- Data is encoded in the **duration of these supply interruptions**. Each LED has a chip
  with a small capacitor that keeps it alive during a pulse. The LED output dims slightly
  while data is being sent (the capacitor cannot fully carry the LED current), which is
  why the stock controller sends frames back to back: a constant duty cycle looks constant.
- All 400 chips see the whole data stream (parallel bus). Each chip has a fixed index,
  evidently programmed at the factory, and picks its own slot from the frame. Consequently
  the curtain cannot be shortened or extended like a WS2812 strip.
- The DC average of `L1-` during a fixed colour is about 2 V (≈40 % of the time high).

## 2. Bit encoding

Measured on `L1-` with the stock controller (0.1 µs resolution). "off" = current
interrupted (line high), "on" = current flowing (line low).

| bit | off (interruption) | on (current) | total |
|-----|--------------------|--------------|-------|
| `0` | 2.4–2.5 µs | 2.7–2.8 µs | ≈5.3 µs |
| `1` | 4.4–4.5 µs | 5.9–6.0 µs | ≈10.4 µs |

The measured "off" widths may be slightly short: the line rises slowly when the transistor
opens, and the capture threshold sits partway up that slope.

What we transmit (`firmware/led_sender` and later): `0` = 2.7 µs off + 2.8 µs on,
`1` = 5.0 µs off + 5.9 µs on. The slightly longer "off" times compensate for the turn-off
delay of a bipolar transistor. With a BC337 these values work; the chips evidently
discriminate on the length of the interruption with a threshold somewhere between
2.7 and 4.4 µs.

Bits are sent **MSB first** within each byte.

## 3. Frame layout

A frame is 9617 bits:

```
+----------+------------------------------------------+----------+
| 00 01    | LED 0: R G B | LED 1: R G B | ... LED 399 | 1 bit    |
| 16 bits  | 400 x 24 bits = 9600 bits                | trailing |
+----------+------------------------------------------+----------+
```

- Header: `0x00 0x01`, identical in every capture (fixed colours, several presets).
  Meaning unknown; sending it unchanged works.
- Per LED three bytes in the order **red, green, blue**. Verified: red preset =
  `1E 00 00`, green = `00 1E 00`, blue = `00 00 1E`.
- One trailing bit after LED 399. Its value was not captured; sending a `0` works.
- After the frame the stock controller keeps the current on for **39.2–39.7 µs**, then the
  next frame starts immediately. A frame takes about 65 ms, so ~15 frames per second.
- Between frames (and when no data is sent at all) the LEDs keep their last colours.

Sending frames with long idle gaps (for example 40 ms) produces visible flicker, because the
LEDs are slightly dimmer during data than during idle. Send frames back to back, or at
least with gaps of a millisecond or less.

## 4. Colour values

- The stock controller uses values **0–30** (0x00–0x1E). Full red/green/blue = 30.
- Its "white" preset is R=15, G=10, B=15: a deliberate mix, presumably to limit current
  and tune the colour.
- A blue-purple preset was R=7, G=0, B=30.
- Whether the chips accept values above 30 is untested. If you try, watch the 5 V rail
  (`L+`) for sagging and keep the transistor and the controller board cool.

## 5. LED order

The curtain is 20 hanging strands of 20 LEDs. Frame slot `i` maps to:

```
strand   = i / 20        (0 = the strand containing LED 0)
position = i % 20        (0 = top, 19 = bottom)
```

So `index(x, y) = x * 20 + y` with `x` the strand and `y` the position from the top.
Whether strand 0 is on your left or right depends on which way the curtain is hung;
the firmware has `MIRROR_X` / `MIRROR_Y` switches.

## 6. Electrical notes for driving it yourself

- Leave the stock board in place as power supply: cut only the curtain's `L1-` wire
  (leave the stub on the pad) and drive that wire with your own low-side switch.
  The board's own transistor keeps pulsing its now unconnected pad; harmless.
- The switch must sink the full curtain current and switch within a fraction of a
  microsecond. A logic-level N-MOSFET (IRLZ44N, IRL540N, AO3400) is the right part.
  A BC337 NPN works up to about value 10 per channel; above that it no longer saturates,
  the supply to the LEDs drops during the "on" periods, and the LEDs start misreading
  bits (white turns into random colours).
- Common ground between the controller board, the switch and the microcontroller is
  essential.
- Timing was generated by bit-banging against the CPU cycle counter with interrupts
  disabled (0.1 µs resolution, no jitter). On the ESP32-S3 this runs on core 0 while
  core 1 handles USB; on a single-core ESP32 it blocks for ~55 ms per frame.

## 7. Compatibility

Not compatible with WS2811/WS2812/SK6812/WS2814, APA102, TM1814 or any other LED type
supported by WLED or FastLED: those all need a separate data (and sometimes clock) line.
A three-wire variant of this curtain exists (same `L+ / L1- / L2-` pads on the board) that
is reported to work with WLED as "SK6812/WS2814 RGBW"; it uses different LED chips.

## 8. Summary table

| Item | Value |
|------|-------|
| Wires | 2 (`L+`, `L1-`) |
| Supply | 5 V on `L+` (from 13 V adapter via the board) |
| Encoding | pulse-width coded supply interruptions |
| `0` | 2.5 µs off, 2.8 µs on |
| `1` | 4.5 µs off, 5.9 µs on |
| Bit order | MSB first |
| Frame | `00 01` + 400 × RGB + 1 bit = 9617 bits |
| Inter-frame gap | ≥ 39 µs, current on |
| Frame rate (stock) | ≈ 15 Hz |
| Value range (stock) | 0–30 per channel |
| LED order | strand-major, top to bottom |
