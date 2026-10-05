#!/usr/bin/env python3
"""
patronen.py - twintig animaties voor de 20x20 ledmatrix (firmware led_matrix_usb).

Gebruik:
  python3 patronen.py --port /dev/cu.usbmodem101
Opties:
  --seconds 15      tijd per patroon
  --only vuur,plasma   alleen deze patronen (komma's, geen spaties)
  --random          willekeurige volgorde
  --maxval 10       helderheid (niet hoger dan MAX_VAL in de firmware)
  --fps 20
  --gamma 1.0
  --list            namen tonen

Eigen patroon toevoegen: schrijf een generator die telkens een beeld oplevert
(px[x][y] = (r, g, b) met waardes 0..1) en zet hem in PATRONEN.
"""

import argparse
import colorsys
import math
import random
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("Eerst: pip3 install pyserial")

W, H = 20, 20
CX, CY = (W - 1) / 2, (H - 1) / 2


# ---------- hulpjes ----------
def hsv(h, s=1.0, v=1.0):
    return colorsys.hsv_to_rgb(h % 1.0, max(0.0, min(1.0, s)), max(0.0, min(1.0, v)))


def blank():
    return [[(0.0, 0.0, 0.0) for _ in range(H)] for _ in range(W)]


def fade(px, f):
    for x in range(W):
        col = px[x]
        for y in range(H):
            r, g, b = col[y]
            col[y] = (r * f, g * f, b * f)


def add(px, x, y, c):
    if 0 <= x < W and 0 <= y < H:
        r, g, b = px[x][y]
        px[x][y] = (min(1.0, r + c[0]), min(1.0, g + c[1]), min(1.0, b + c[2]))


# ---------- patronen (elk een generator die beelden oplevert) ----------
def plasma():
    t = 0.0
    while True:
        px = blank()
        for x in range(W):
            for y in range(H):
                v = (math.sin(x * 0.5 + t) + math.sin(y * 0.4 - t * 0.7)
                     + math.sin((x + y) * 0.3 + t * 0.5)
                     + math.sin(math.hypot(x - CX, y - CY) * 0.6 - t))
                px[x][y] = hsv(v / 8 + t * 0.05)
        t += 0.1
        yield px


def regenboog():
    t = 0.0
    while True:
        px = blank()
        for x in range(W):
            for y in range(H):
                px[x][y] = hsv((x + y) / 30 + t)
        t += 0.012
        yield px


def vuur():
    heat = [[0.0] * H for _ in range(W)]
    while True:
        for x in range(W):
            heat[x][H - 1] = random.uniform(0.55, 1.0)
            for y in range(H - 1):
                below = heat[x][y + 1]
                left = heat[x - 1][y + 1] if x > 0 else below
                right = heat[x + 1][y + 1] if x < W - 1 else below
                heat[x][y] = max(0.0, (below * 2 + left + right) / 4 - random.uniform(0.03, 0.13))
        px = blank()
        for x in range(W):
            for y in range(H):
                h = heat[x][y]
                px[x][y] = (min(1.0, h * 2), min(1.0, max(0.0, h - 0.4) * 1.6), max(0.0, h - 0.85) * 4)
        yield px


def matrix():
    drops = [[random.uniform(-H, 0), random.uniform(0.3, 1.0)] for _ in range(W)]
    px = blank()
    while True:
        fade(px, 0.75)
        for x, d in enumerate(drops):
            d[0] += d[1]
            y = int(d[0])
            add(px, x, y, (0.6, 1.0, 0.6))
            add(px, x, y - 1, (0.0, 0.6, 0.0))
            if d[0] > H + 3:
                d[0], d[1] = random.uniform(-10, 0), random.uniform(0.3, 1.0)
        yield px


def sterren():
    px = blank()
    while True:
        fade(px, 0.9)
        if random.random() < 0.7:
            add(px, random.randrange(W), random.randrange(H), hsv(random.random(), 0.3, 1.0))
        yield px


def ballen():
    balls = []
    for i in range(3):
        balls.append([random.uniform(0, W - 1), random.uniform(0, H - 1),
                      random.choice([-1, 1]) * random.uniform(0.25, 0.5),
                      random.choice([-1, 1]) * random.uniform(0.25, 0.5), hsv(i / 3)])
    px = blank()
    while True:
        fade(px, 0.7)
        for b in balls:
            b[0] += b[2]
            b[1] += b[3]
            if b[0] < 0 or b[0] > W - 1:
                b[2] *= -1
                b[0] = max(0, min(W - 1, b[0]))
            if b[1] < 0 or b[1] > H - 1:
                b[3] *= -1
                b[1] = max(0, min(H - 1, b[1]))
            add(px, int(round(b[0])), int(round(b[1])), b[4])
        yield px


def spiraal():
    t = 0.0
    while True:
        px = blank()
        for x in range(W):
            for y in range(H):
                dx, dy = x - CX, y - CY
                a, r = math.atan2(dy, dx), math.hypot(dx, dy)
                v = (math.sin(a * 3 + r * 0.6 - t * 2) + 1) / 2
                px[x][y] = hsv(r / 14 + t * 0.1, 1.0, v)
        t += 0.1
        yield px


def rimpels():
    rings = []
    while True:
        if random.random() < 0.07:
            rings.append([random.uniform(2, W - 3), random.uniform(2, H - 3), 0.0, random.random()])
        px = blank()
        for ring in rings:
            ring[2] += 0.4
            for x in range(W):
                for y in range(H):
                    d = abs(math.hypot(x - ring[0], y - ring[1]) - ring[2])
                    if d < 1.2:
                        add(px, x, y, hsv(ring[3], 1.0, (1.2 - d) / 1.2 * max(0.0, 1 - ring[2] / 16)))
        rings = [r for r in rings if r[2] < 16]
        yield px


def leven():
    def zaai():
        return [[random.random() < 0.35 for _ in range(H)] for _ in range(W)]
    cells, age, hist, n_frames = zaai(), [[0] * H for _ in range(W)], [], 0
    while True:
        new = [[False] * H for _ in range(W)]
        for x in range(W):
            for y in range(H):
                n = sum(cells[(x + dx) % W][(y + dy) % H]
                        for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy)
                new[x][y] = n == 3 or (cells[x][y] and n == 2)
                age[x][y] = age[x][y] + 1 if new[x][y] else 0
        cells = new
        hist = (hist + [sum(sum(c) for c in cells)])[-12:]
        n_frames += 1
        if (len(hist) == 12 and len(set(hist)) <= 2) or n_frames > 400:
            cells, hist, n_frames = zaai(), [], 0
        px = blank()
        for x in range(W):
            for y in range(H):
                if cells[x][y]:
                    px[x][y] = hsv(0.5 + min(age[x][y], 30) / 40)
        yield px


def slang():
    x, y, h, px = W // 2, H // 2, 0.0, blank()
    while True:
        fade(px, 0.85)
        dx, dy = random.choice([(1, 0), (-1, 0), (0, 1), (0, -1)])
        x, y = max(0, min(W - 1, x + dx)), max(0, min(H - 1, y + dy))
        h += 0.01
        add(px, x, y, hsv(h))
        yield px


def balken():
    t = 0.0
    while True:
        px = blank()
        for x in range(W):
            for y in range(H):
                v = (math.sin((x - t) * 0.8) + 1) / 2
                px[x][y] = hsv(t * 0.02 + y / 40, 1.0, v ** 3)
        t += 0.3
        yield px


def ademen():
    t = 0.0
    while True:
        v = (math.sin(t) + 1) / 2
        c = hsv(t / 20, 1.0, 0.05 + 0.95 * v)
        px = [[c] * H for _ in range(W)]
        t += 0.08
        yield px


def radar():
    t, px = 0.0, blank()
    while True:
        fade(px, 0.88)
        for r in range(0, 14):
            add(px, int(round(CX + r * math.cos(t))), int(round(CY + r * math.sin(t))), (0.2, 1.0, 0.3))
        t += 0.15
        yield px


HART = [
    "..XXX...XXX..",
    ".XXXXX.XXXXX.",
    "XXXXXXXXXXXXX",
    "XXXXXXXXXXXXX",
    ".XXXXXXXXXXX.",
    "..XXXXXXXXX..",
    "...XXXXXXX...",
    "....XXXXX....",
    ".....XXX.....",
    "......X......",
]


def hart():
    t = 0.0
    while True:
        p = t % 1.0
        v = 1.0 if p < 0.12 or 0.22 < p < 0.34 else 0.35    # boem-boem
        px = blank()
        for j, row in enumerate(HART):
            for i, ch in enumerate(row):
                if ch == "X":
                    px[i + 3][j + 5] = (v, 0.0, v * 0.15)
        t += 0.05
        yield px


def schaakbord():
    t = 0.0
    while True:
        px, o = blank(), int(t)
        for x in range(W):
            for y in range(H):
                if (((x + o) // 4) + ((y + o) // 4)) % 2 == 0:
                    px[x][y] = hsv(t * 0.03, 1.0, 0.8)
                else:
                    px[x][y] = hsv(t * 0.03 + 0.5, 1.0, 0.25)
        t += 0.25
        yield px


def regen():
    drops, px = [], blank()
    while True:
        fade(px, 0.7)
        if random.random() < 0.5:
            drops.append([random.randrange(W), 0.0, random.uniform(0.4, 0.9)])
        for d in drops:
            d[1] += d[2]
            add(px, d[0], int(d[1]), (0.2, 0.4, 1.0))
        drops = [d for d in drops if d[1] < H]
        yield px


def lava():
    seeds = [(random.uniform(0, W), random.uniform(0, H), random.uniform(0.5, 1.5), random.random() * 6)
             for _ in range(5)]
    t = 0.0
    while True:
        px = blank()
        for x in range(W):
            for y in range(H):
                v = sum(math.sin(math.hypot(x - sx, y - sy) * f * 0.4 - t + ph) for sx, sy, f, ph in seeds)
                v = (v / 5 + 1) / 2
                px[x][y] = (v, v * 0.3, (1 - v) * 0.6)
        t += 0.12
        yield px


def komeet():
    t, px = 0.0, blank()
    while True:
        fade(px, 0.8)
        add(px, int(round(CX + 9 * math.cos(t))), int(round(CY + 9 * math.sin(t * 1.3))), hsv(t * 0.05))
        t += 0.12
        yield px


def confetti():
    px = blank()
    while True:
        fade(px, 0.93)
        for _ in range(3):
            add(px, random.randrange(W), random.randrange(H), hsv(random.random()))
        yield px


def sinus():
    t, px = 0.0, blank()
    while True:
        fade(px, 0.6)
        for x in range(W):
            add(px, x, int(round(CY + 7 * math.sin(x * 0.5 + t))), hsv(x / W + t * 0.1))
        t += 0.2
        yield px


PATRONEN = {
    "plasma": plasma, "regenboog": regenboog, "vuur": vuur, "matrix": matrix,
    "sterren": sterren, "ballen": ballen, "spiraal": spiraal, "rimpels": rimpels,
    "leven": leven, "slang": slang, "balken": balken, "ademen": ademen,
    "radar": radar, "hart": hart, "schaakbord": schaakbord, "regen": regen,
    "lava": lava, "komeet": komeet, "confetti": confetti, "sinus": sinus,
}


# ---------- zenden ----------
def to_bytes(px, maxval, gamma):
    out = bytearray()
    for x in range(W):
        for y in range(H):
            for v in px[x][y]:
                f = max(0.0, min(1.0, v)) ** gamma
                out.append(min(maxval, int(round(f * maxval))))
    return bytes(out)


def send(ser, data):
    packet = b"\xff" + data
    for i in range(0, len(packet), 240):
        ser.write(packet[i:i + 240])
        ser.flush()
        time.sleep(0.003)


def main():
    ap = argparse.ArgumentParser(description="Animaties voor de 20x20 ledmatrix")
    ap.add_argument("--port", help="bijv. /dev/cu.usbmodem101")
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--only", help="namen, gescheiden door komma's")
    ap.add_argument("--random", action="store_true")
    ap.add_argument("--maxval", type=int, default=10)
    ap.add_argument("--fps", type=float, default=20)
    ap.add_argument("--gamma", type=float, default=1.0)
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        print(", ".join(PATRONEN))
        return
    if not args.port:
        sys.exit("--port is verplicht (of --list)")
    if not 1 <= args.maxval <= 30:
        sys.exit("--maxval moet tussen 1 en 30 liggen")

    namen = list(PATRONEN)
    if args.only:
        namen = [n.strip() for n in args.only.split(",")]
        onbekend = [n for n in namen if n not in PATRONEN]
        if onbekend:
            sys.exit("onbekend patroon: " + ", ".join(onbekend))

    ser = serial.Serial(args.port, 115200, timeout=1)
    time.sleep(0.5)
    interval = 1.0 / args.fps
    try:
        while True:
            volgorde = namen[:]
            if args.random:
                random.shuffle(volgorde)
            for naam in volgorde:
                print(naam)
                gen = PATRONEN[naam]()
                einde = time.time() + args.seconds
                while time.time() < einde:
                    t0 = time.time()
                    send(ser, to_bytes(next(gen), args.maxval, args.gamma))
                    time.sleep(max(0.0, interval - (time.time() - t0)))
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
