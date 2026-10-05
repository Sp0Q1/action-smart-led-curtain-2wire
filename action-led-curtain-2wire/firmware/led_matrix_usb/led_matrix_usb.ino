// led_matrix_usb.ino  (v3)
// 20x20 Action-ledsnoer als USB-scherm. Zenden draait op kern 0, zodat kern 1
// vrij is voor USB en de seriële commando's.
//
// Binaire frames via USB:  0xFF gevolgd door 1200 bytes R G B per pixel,
//   volgorde: streng 0 van boven naar beneden, dan streng 1, enz. (zie gif2matrix.py)
//   Pixelwaardes 0..MAX_VAL (hoger wordt afgekapt, nooit 0xFF).
// Tekstcommando's (Serial Monitor, Newline aan):
//   f r g b | p x y r g b | c | d

#include <Arduino.h>
#include "soc/gpio_reg.h"
#include "driver/gpio.h"

#define OUT_PIN    6
#define W          20
#define H          20
#define NUM_LEDS   (W * H)
#define MAX_VAL    10      // origineel gaat tot 30; met de BC337 laag houden
#define INVERT     0
#define TRAIL_BIT  0
#define MIRROR_X   0
#define MIRROR_Y   0

#define T0_OFF 27          // pulsbreedtes in 0,1 us
#define T0_ON  28
#define T1_OFF 50
#define T1_ON  59

static uint8_t frame[2 + NUM_LEDS * 3];        // wat kern 0 uitzendt
static uint8_t pend[2][NUM_LEDS * 3];          // dubbele buffer voor nieuwe beelden
static volatile int  pendReady = -1;           // index van buffer die klaarstaat, -1 = niets
static int  pendWrite = 0;
static uint32_t cyc100ns;
static const uint32_t pinMask = 1UL << OUT_PIN;
static bool demoOn = true;

// ---------- zenden (kern 0) ----------
static inline void stroomAan() {
  if (INVERT) REG_WRITE(GPIO_OUT_W1TC_REG, pinMask); else REG_WRITE(GPIO_OUT_W1TS_REG, pinMask);
}
static inline void stroomUit() {
  if (INVERT) REG_WRITE(GPIO_OUT_W1TS_REG, pinMask); else REG_WRITE(GPIO_OUT_W1TC_REG, pinMask);
}
static inline void wachtTot(uint32_t t) {
  while ((int32_t)(ESP.getCycleCount() - t) < 0) { }
}
static inline void sendBit(bool one, uint32_t &t) {
  stroomUit();
  t += (one ? T1_OFF : T0_OFF) * cyc100ns;
  wachtTot(t);
  stroomAan();
  t += (one ? T1_ON : T0_ON) * cyc100ns;
  wachtTot(t);
}
static void sendFrame() {
  portDISABLE_INTERRUPTS();
  uint32_t t = ESP.getCycleCount();
  for (size_t i = 0; i < sizeof(frame); i++) {
    uint8_t b = frame[i];
    for (int k = 7; k >= 0; k--) sendBit((b >> k) & 1, t);
  }
  sendBit(TRAIL_BIT, t);
  portENABLE_INTERRUPTS();
}
static void senderTask(void *) {
  for (;;) {
    int r = pendReady;
    if (r >= 0) {
      memcpy(frame + 2, pend[r], NUM_LEDS * 3);
      pendReady = -1;
    }
    sendFrame();
    vTaskDelay(1);                     // even lucht voor kern 0
  }
}

// ---------- tekenen (kern 1, in de schrijfbuffer) ----------
static inline uint8_t clampVal(int v) {
  if (v < 0) return 0;
  if (v > MAX_VAL) return MAX_VAL;
  return (uint8_t)v;
}
int ledIndex(int x, int y) {
  if (MIRROR_X) x = W - 1 - x;
  if (MIRROR_Y) y = H - 1 - y;
  return x * H + y;                    // streng x, positie y van boven
}
void setPixel(int x, int y, int r, int g, int b) {
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  int i = ledIndex(x, y);
  pend[pendWrite][3 * i]     = clampVal(r);
  pend[pendWrite][3 * i + 1] = clampVal(g);
  pend[pendWrite][3 * i + 2] = clampVal(b);
}
void fillAll(int r, int g, int b) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) setPixel(x, y, r, g, b);
}
void clearAll() { fillAll(0, 0, 0); }
void show() {                          // schrijfbuffer aanbieden aan kern 0
  int w = pendWrite;
  pendWrite ^= 1;
  memcpy(pend[pendWrite], pend[w], NUM_LEDS * 3);   // nieuwe schrijfbuffer start met huidig beeld
  pendReady = w;
}

void hueToRgb(int hue, int &r, int &g, int &b) {
  hue &= 255;
  int seg = hue / 43, rest = (hue % 43) * 6;
  int up = rest, down = 255 - rest;
  int R = 0, G = 0, B = 0;
  switch (seg) {
    case 0: R = 255;  G = up;   break;
    case 1: R = down; G = 255;  break;
    case 2: G = 255;  B = up;   break;
    case 3: G = down; B = 255;  break;
    case 4: B = 255;  R = up;   break;
    default: B = down; R = 255; break;
  }
  r = R * MAX_VAL / 255; g = G * MAX_VAL / 255; b = B * MAX_VAL / 255;
}

// ---------- demo ----------
void demoStep() {
  static uint32_t step = 0;
  uint32_t fase = (step / 60) % 4;
  int s = step % 60;
  clearAll();
  if (fase == 0) {
    for (int i = 0; i < W; i++) { setPixel(i, 0, MAX_VAL, 0, 0); setPixel(i, H - 1, MAX_VAL, 0, 0); }
    for (int i = 0; i < H; i++) { setPixel(0, i, 0, 0, MAX_VAL); setPixel(W - 1, i, 0, 0, MAX_VAL); }
  } else if (fase == 1) {
    for (int i = 0; i < W; i++) setPixel(i, i, 0, MAX_VAL, 0);
    setPixel(s % W, s % W, MAX_VAL, MAX_VAL, MAX_VAL);
  } else if (fase == 2) {
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        int r, g, b; hueToRgb((x + y) * 8 + s * 6, r, g, b);
        setPixel(x, y, r, g, b);
      }
  } else {
    int x = s % (2 * (W - 1)); if (x >= W) x = 2 * (W - 1) - x;
    int y = (s * 3) % (2 * (H - 1)); if (y >= H) y = 2 * (H - 1) - y;
    setPixel(x, y, MAX_VAL, 0, MAX_VAL / 2);
  }
  show();
  step++;
}

// ---------- USB: binaire frames en tekstcommando's ----------
static bool inFrame = false;
static int  frameFill = 0;

void handleLine(char *line) {
  int a, b, c, d, e;
  if (line[0] == 'f' && sscanf(line + 1, "%d %d %d", &a, &b, &c) == 3) {
    demoOn = false; fillAll(a, b, c); show(); Serial.println("ok vul");
  } else if (line[0] == 'p' && sscanf(line + 1, "%d %d %d %d %d", &a, &b, &c, &d, &e) == 5) {
    demoOn = false; setPixel(a, b, c, d, e); show(); Serial.println("ok pixel");
  } else if (line[0] == 'c') {
    demoOn = false; clearAll(); show(); Serial.println("ok uit");
  } else if (line[0] == 'd') {
    demoOn = !demoOn; Serial.println(demoOn ? "demo aan" : "demo uit");
  } else {
    Serial.println("commando's: f r g b | p x y r g b | c | d | binair: 0xFF + 1200 bytes");
  }
}

void pollSerial() {
  static char buf[48];
  static int n = 0;
  while (Serial.available()) {
    uint8_t ch = (uint8_t)Serial.read();
    if (ch == 0xFF) {                  // start van een binair frame
      inFrame = true; frameFill = 0; n = 0;
      continue;
    }
    if (inFrame) {
      // frame komt binnen in volgorde: streng 0 boven->onder, streng 1, ...
      int i = frameFill / 3, ch_i = frameFill % 3;
      int x = i / H, y = i % H;
      int li = ledIndex(x, y);
      pend[pendWrite][3 * li + ch_i] = clampVal(ch);
      if (++frameFill >= NUM_LEDS * 3) {
        inFrame = false;
        demoOn = false;
        show();
      }
      continue;
    }
    if (ch == '\n' || ch == '\r') {
      if (n > 0) { buf[n] = 0; handleLine(buf); n = 0; }
    } else if (n < (int)sizeof(buf) - 1) {
      buf[n++] = (char)ch;
    }
  }
}

void setup() {
  Serial.setRxBufferSize(8192);        // 1200-byte frames komen in één klap binnen
  Serial.begin(115200);
  pinMode(OUT_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)OUT_PIN, GPIO_DRIVE_CAP_3);
  stroomAan();
  cyc100ns = getCpuFrequencyMhz() / 10;
  frame[0] = 0x00;
  frame[1] = 0x01;
  memset(frame + 2, 0, NUM_LEDS * 3);
  memset(pend, 0, sizeof(pend));
  xTaskCreatePinnedToCore(senderTask, "sender", 4096, NULL, 2, NULL, 0);
  delay(500);
  Serial.printf("led_matrix_usb v3: %dx%d, MAX_VAL %d. Demo aan; stuur een frame of typ d.\n", W, H, MAX_VAL);
}

void loop() {
  pollSerial();
  if (demoOn) {
    static uint32_t last = 0;
    if (millis() - last >= 60) { last = millis(); demoStep(); }
  }
  delay(1);
}
