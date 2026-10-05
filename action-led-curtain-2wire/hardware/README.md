# Hardware notes

Fill in / add here:

- `controller-top.jpg`, `controller-pads.jpg`: photos of the controller PCB (silkscreen **KR-026-R1**).
- Product: Action LED curtain, 400 LEDs (20 strands x 20), ___ (product name / EAN from the box).
- Adapter: ___ V DC, ___ A (label on the plug-in adapter). Measured 13 V at the VIN pad.
- Controller features: IR remote receiver, microphone, push button, PCB-trace antenna (probably Bluetooth), 24 MHz crystal, 16-pin MCU (marking ___).
- Output pads: `L+` (5 V to the curtain), `L1-` (switched return = data), `L2-` (unused on this unit).

## Pad map (as seen on the PCB)

| Pad | Function | Measured (stock controller running) |
|-----|----------|-------------------------------------|
| VIN | adapter + | 13 V |
| GND | adapter - | 0 V (reference) |
| L+  | curtain + | 4.96 V at full-brightness white |
| L1- | curtain return, pulsed | ~2.0 V average (DMM) |
| L2- | not connected on the 2-wire unit | 0 V, open to GND |
