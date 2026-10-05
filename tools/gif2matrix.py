#!/usr/bin/env python3
"""
gif2matrix.py - stuur een GIF (of PNG/JPG) als 20x20 beelden naar de badge.

Gebruik:
  python3 gif2matrix.py plaatje.gif --port /dev/cu.usbmodem101
Opties:
  --maxval 10   helderheid (niet hoger dan MAX_VAL in de firmware)
  --fps 8       vast tempo in plaats van het tempo uit de GIF
  --once        animatie één keer afspelen in plaats van herhalen
  --gamma 1.0   >1 maakt donkere tinten donkerder (bijv. 1.6)

Eenmalig: pip3 install pyserial pillow
Serial Monitor in de Arduino IDE sluiten voordat je dit start.
"""

import argparse
import sys
import time

try:
    import serial
    from PIL import Image, ImageSequence
except ImportError:
    sys.exit("Eerst: pip3 install pyserial pillow")

W, H = 20, 20


def frame_bytes(img, maxval, gamma):
    """Zet een PIL-beeld om in 1200 bytes: streng 0 boven->onder, streng 1, ..."""
    rgb = Image.new("RGB", img.size, (0, 0, 0))
    rgb.paste(img.convert("RGBA"), mask=img.convert("RGBA").split()[3])
    small = rgb.resize((W, H), Image.BOX)
    px = small.load()
    out = bytearray()
    for x in range(W):
        for y in range(H):
            r, g, b = px[x, y]
            for v in (r, g, b):
                f = (v / 255.0) ** gamma
                out.append(min(maxval, int(round(f * maxval))))
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="GIF/PNG naar de 20x20 ledmatrix")
    ap.add_argument("bestand")
    ap.add_argument("--port", required=True, help="bijv. /dev/cu.usbmodem101")
    ap.add_argument("--maxval", type=int, default=10)
    ap.add_argument("--fps", type=float, default=None)
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--gamma", type=float, default=1.0)
    args = ap.parse_args()

    if not 1 <= args.maxval <= 30:
        sys.exit("--maxval moet tussen 1 en 30 liggen")

    img = Image.open(args.bestand)
    frames = []
    for fr in ImageSequence.Iterator(img):
        dur = fr.info.get("duration", 100) / 1000.0
        frames.append((frame_bytes(fr, args.maxval, args.gamma), dur))
    print(f"{len(frames)} frame(s) uit {args.bestand}")

    ser = serial.Serial(args.port, 115200, timeout=1)
    time.sleep(0.5)

    try:
        while True:
            for data, dur in frames:
                packet = b"\xff" + data
                for i in range(0, len(packet), 240):     # in porties, badge leest 1x per ms
                    ser.write(packet[i:i + 240])
                    ser.flush()
                    time.sleep(0.003)
                wait = (1.0 / args.fps) if args.fps else max(dur, 0.07)
                time.sleep(wait)
            if args.once or len(frames) == 1:
                break
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
