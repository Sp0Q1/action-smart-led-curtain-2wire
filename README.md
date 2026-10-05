# Action LED curtain (400 LEDs, 20×20) — 2-wire protocol, reverse engineered

The cheap "LED curtain" sold by Action (Dutch discount store) has 400 individually
addressable RGB LEDs, but it is **not** WS2812-compatible: the LEDs hang on only **two
wires** and receive their colour data as short interruptions of the supply current.
This repository documents that protocol and shows how to drive the curtain yourself
with any ESP32 and a single transistor.

Result: full per-pixel control at ~15 frames per second, GIF playback over USB, and
twenty built-in animations that run on the ESP32 alone.

*(Nederlandse samenvatting onderaan.)*

## What's in here

| Path | Contents |
|------|----------|
| `docs/protocol.md` | The protocol: bit timing, frame layout, colour values, LED order, open questions |
| `docs/reverse-engineering.md` | How it was figured out with a multimeter and an ESP32 as a 10 MS/s logic analyzer |
| `docs/wiring.svg` | Schematic for the transistor stage |
| `docs/captures/` | Raw captures from the stock controller (red, green, blue, white, pulse widths) |
| `firmware/pulse_capture` | ESP32 sketch: sample a pin at ~10 MHz and print pulse widths |
| `firmware/pulse_decode` | ESP32 sketch: decode short/long pulses into bits and print frames as hex |
| `firmware/led_sender` | Minimal sender: drives the curtain with the reverse-engineered protocol |
| `firmware/led_matrix_usb` | Sender on core 0, USB frame receiver and text commands on core 1 |
| `firmware/led_matrix_patronen` | Same, plus twenty built-in animations (standalone operation) |
| `tools/gif2matrix.py` | Play any GIF/PNG on the curtain over USB |
| `tools/patronen.py` | Twenty procedural animations rendered on the PC, streamed over USB |
| `hardware/` | Photos and notes on the stock controller board (silkscreen KR-026-R1) |

## Protocol in one paragraph

All 400 LEDs sit in parallel across two wires: `L+` (5 V, generated on the controller
board from a 13 V adapter) and `L1-`, a return line that the controller switches to ground
with a transistor. Data is sent by **interrupting the current**: an interruption of
2.5 µs is a `0`, one of 4.5 µs is a `1`; each bit is followed by 2.8 µs (for a `0`) or
5.9 µs (for a `1`) of current. Every LED contains a chip with a small capacitor, so it
rides through the gaps and only sees the message. A frame is `0x00 0x01`, then
400 × (R, G, B) with values 0–30, then one extra bit, then at least ~40 µs of
uninterrupted current. Each LED chip has a fixed index programmed at the factory, so
it knows which three bytes are its own. Full details in [`docs/protocol.md`](docs/protocol.md).

## Quick start

Hardware (any ESP32 works; we used an ESP32-S3):

- Cut the thin `L1-` wire of the curtain a few cm from the controller board. Leave `L+`
  and the board's power connected: the board keeps supplying the 5 V.
- Connect the curtain's `L1-` wire to the **collector** of an NPN transistor (BC337 works
  for low brightness) or the **drain** of a logic-level N-MOSFET (IRLZ44N, AO3400: needed
  for full brightness).
- Emitter/source to ground. ESP32 ground, board ground and transistor ground all joined.
- ESP32 GPIO → 220 Ω → base (NPN) or 100 Ω → gate (MOSFET); 10 kΩ from base/gate to ground.

See [`docs/wiring.svg`](docs/wiring.svg).

Software:

1. Flash `firmware/led_matrix_patronen` (Arduino IDE, board "ESP32S3 Dev Module" or your
   ESP32; set `OUT_PIN`, `MAX_VAL`). It starts cycling through twenty animations.
2. Optional: `pip3 install pyserial pillow`, then
   `python3 tools/gif2matrix.py animation.gif --port /dev/cu.usbmodem101` to play a GIF, or
   `python3 tools/patronen.py --port ... --random` for PC-rendered animations.

Keep `MAX_VAL` at 10 with a small NPN transistor; with a MOSFET use 30 (what the stock
controller uses). Values above 30 are untested.

## Variants

The controller board has three output pads, `L+`, `L1-` and `L2-`. On the unit documented
here only `L+` and `L1-` are used (two wires to the curtain). Another owner reported a
**three-wire** curtain from the same shop that works directly with WLED configured as
"SK6812/WS2814 RGBW" (colour order BRG, swap W and G). That is a different LED chip
generation; check how many wires leave your controller before assuming anything.
If you document another unit, please add the silkscreen code of the board, the number of
wires and the frame layout.

## Open questions

- Meaning of the header bytes `0x00 0x01` (identical in every capture).
- Value of the trailing bit after the last LED (sending `0` works).
- Whether the LED chips accept values above 30 (the stock controller never sends more).
- Exact current draw at full brightness.

## Credits

- Tim ([cpldcpu](https://cpldcpu.com)) reverse engineered the first generation of these
  power-line controlled strings in 2022 ("Controlling RGB LEDs with only the powerlines");
  that work established the mechanism. This curtain is a later generation: binary pulse
  coding, per-LED addressing and 31 levels per channel instead of pulse counting, 6 zones
  and on/off colours.
- These strings were first discussed in WLED issue #1312.

## License

MIT, see `LICENSE`.

---

## Nederlandse samenvatting

Het ledgordijn van de Action (400 leds, 20 strengen van 20) is per led aanstuurbaar, maar
niet WS2812-compatibel: de leds hangen aan twee draden en krijgen hun kleur via korte
onderbrekingen van de stroom (2,5 µs = 0, 4,5 µs = 1). Een beeld is `00 01`, dan per led
drie bytes rood/groen/blauw (0–30), dan één los bit. Met een ESP32, één transistor in de
terugleiding en de firmware uit deze repository stuur je het gordijn zelf aan: per pixel,
15 beelden per seconde, met GIF's via USB of ingebouwde animaties. Hoe het is uitgezocht
staat in `docs/reverse-engineering.md`.
