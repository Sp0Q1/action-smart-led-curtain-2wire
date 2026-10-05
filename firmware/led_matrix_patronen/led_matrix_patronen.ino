// led_matrix_patronen.ino  (v4)
// Twintig animaties op de badge zelf: alleen stroom erop en hij draait.
// Zenden op kern 0, animaties en USB op kern 1.
//
// Serial Monitor (115200, Newline aan):
//   n            volgend patroon
//   k 5          kies patroon 5 en blijf daarop
//   a            weer automatisch wisselen
//   l            lijst van patronen
//   f r g b | p x y r g b | c      zoals eerder (zet animaties uit)
//   d            animaties weer aan
// Binaire frames (0xFF + 1200 bytes, zie gif2matrix.py) werken ook nog.

#include <Arduino.h>
#include <math.h>
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

#define PATROON_SECONDEN 15     // tijd per patroon
#define FRAME_INTERVAL_MS 50    // 20 beelden per seconde

#define T0_OFF 27
#define T0_ON  28
#define T1_OFF 50
#define T1_ON  59

static const float CX = (W - 1) / 2.0f, CY = (H - 1) / 2.0f;

// ---------- zenden (kern 0) ----------
static uint8_t frame[2 + NUM_LEDS * 3];
static uint8_t pend[2][NUM_LEDS * 3];
static volatile int pendReady = -1;
static int pendWrite = 0;
static uint32_t cyc100ns;
static const uint32_t pinMask = 1UL << OUT_PIN;

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
    vTaskDelay(1);
  }
}

// ---------- pixels (kern 1) ----------
static inline uint8_t clampVal(int v) {
  if (v < 0) return 0;
  if (v > MAX_VAL) return MAX_VAL;
  return (uint8_t)v;
}
int ledIndex(int x, int y) {
  if (MIRROR_X) x = W - 1 - x;
  if (MIRROR_Y) y = H - 1 - y;
  return x * H + y;
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
void show() {
  int w = pendWrite;
  pendWrite ^= 1;
  memcpy(pend[pendWrite], pend[w], NUM_LEDS * 3);
  pendReady = w;
}

// ---------- canvas met waardes 0..1 ----------
static float cv[W][H][3];

static float frand() { return random(0, 10000) / 10000.0f; }
static float frandR(float a, float b) { return a + (b - a) * frand(); }
static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

static void hsv2rgb(float h, float s, float v, float &r, float &g, float &b) {
  h -= floorf(h);
  s = clamp01(s); v = clamp01(v);
  float i = floorf(h * 6), f = h * 6 - i;
  float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  switch (((int)i) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
}
static void cvClear() { memset(cv, 0, sizeof(cv)); }
static void cvFade(float f) {
  for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) for (int c = 0; c < 3; c++) cv[x][y][c] *= f;
}
static void cvSet(int x, int y, float r, float g, float b) {
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  cv[x][y][0] = r; cv[x][y][1] = g; cv[x][y][2] = b;
}
static void cvSetHsv(int x, int y, float h, float s, float v) {
  float r, g, b; hsv2rgb(h, s, v, r, g, b); cvSet(x, y, r, g, b);
}
static void cvAdd(int x, int y, float r, float g, float b) {
  if (x < 0 || x >= W || y < 0 || y >= H) return;
  cv[x][y][0] = clamp01(cv[x][y][0] + r);
  cv[x][y][1] = clamp01(cv[x][y][1] + g);
  cv[x][y][2] = clamp01(cv[x][y][2] + b);
}
static void cvAddHsv(int x, int y, float h, float s, float v) {
  float r, g, b; hsv2rgb(h, s, v, r, g, b); cvAdd(x, y, r, g, b);
}
static void cvShow() {
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++)
      setPixel(x, y, (int)lroundf(clamp01(cv[x][y][0]) * MAX_VAL),
                     (int)lroundf(clamp01(cv[x][y][1]) * MAX_VAL),
                     (int)lroundf(clamp01(cv[x][y][2]) * MAX_VAL));
  show();
}

// ---------- de patronen: (stap, eerste keer?) ----------
static void pPlasma(uint32_t s, bool first) {
  float t = s * 0.1f;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      float v = sinf(x * 0.5f + t) + sinf(y * 0.4f - t * 0.7f)
              + sinf((x + y) * 0.3f + t * 0.5f) + sinf(hypotf(x - CX, y - CY) * 0.6f - t);
      cvSetHsv(x, y, v / 8 + t * 0.05f, 1, 1);
    }
}

static void pRegenboog(uint32_t s, bool first) {
  float t = s * 0.012f;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) cvSetHsv(x, y, (x + y) / 30.0f + t, 1, 1);
}

static float heat[W][H];
static void pVuur(uint32_t s, bool first) {
  if (first) memset(heat, 0, sizeof(heat));
  for (int x = 0; x < W; x++) {
    heat[x][H - 1] = frandR(0.55f, 1.0f);
    for (int y = 0; y < H - 1; y++) {
      float below = heat[x][y + 1];
      float left = x > 0 ? heat[x - 1][y + 1] : below;
      float right = x < W - 1 ? heat[x + 1][y + 1] : below;
      heat[x][y] = fmaxf(0, (below * 2 + left + right) / 4 - frandR(0.03f, 0.13f));
    }
  }
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      float h = heat[x][y];
      cvSet(x, y, fminf(1, h * 2), fminf(1, fmaxf(0, h - 0.4f) * 1.6f), fmaxf(0, h - 0.85f) * 4);
    }
}

static float dropY[W], dropV[W];
static void pMatrix(uint32_t s, bool first) {
  if (first) { cvClear(); for (int x = 0; x < W; x++) { dropY[x] = frandR(-H, 0); dropV[x] = frandR(0.3f, 1.0f); } }
  cvFade(0.75f);
  for (int x = 0; x < W; x++) {
    dropY[x] += dropV[x];
    int y = (int)floorf(dropY[x]);
    cvAdd(x, y, 0.6f, 1, 0.6f);
    cvAdd(x, y - 1, 0, 0.6f, 0);
    if (dropY[x] > H + 3) { dropY[x] = frandR(-10, 0); dropV[x] = frandR(0.3f, 1.0f); }
  }
}

static void pSterren(uint32_t s, bool first) {
  if (first) cvClear();
  cvFade(0.9f);
  if (frand() < 0.7f) cvAddHsv(random(W), random(H), frand(), 0.3f, 1);
}

static float ball[3][4]; static float ballHue[3];
static void pBallen(uint32_t s, bool first) {
  if (first) {
    cvClear();
    for (int i = 0; i < 3; i++) {
      ball[i][0] = frandR(0, W - 1); ball[i][1] = frandR(0, H - 1);
      ball[i][2] = (random(2) ? 1 : -1) * frandR(0.25f, 0.5f);
      ball[i][3] = (random(2) ? 1 : -1) * frandR(0.25f, 0.5f);
      ballHue[i] = i / 3.0f;
    }
  }
  cvFade(0.7f);
  for (int i = 0; i < 3; i++) {
    float *b = ball[i];
    b[0] += b[2]; b[1] += b[3];
    if (b[0] < 0 || b[0] > W - 1) { b[2] = -b[2]; b[0] = fmaxf(0, fminf(W - 1, b[0])); }
    if (b[1] < 0 || b[1] > H - 1) { b[3] = -b[3]; b[1] = fmaxf(0, fminf(H - 1, b[1])); }
    cvAddHsv((int)lroundf(b[0]), (int)lroundf(b[1]), ballHue[i], 1, 1);
  }
}

static void pSpiraal(uint32_t s, bool first) {
  float t = s * 0.1f;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      float dx = x - CX, dy = y - CY;
      float a = atan2f(dy, dx), r = hypotf(dx, dy);
      float v = (sinf(a * 3 + r * 0.6f - t * 2) + 1) / 2;
      cvSetHsv(x, y, r / 14 + t * 0.1f, 1, v);
    }
}

#define MAX_RINGS 8
static float ring[MAX_RINGS][4]; static bool ringOn[MAX_RINGS];
static void pRimpels(uint32_t s, bool first) {
  if (first) for (int i = 0; i < MAX_RINGS; i++) ringOn[i] = false;
  if (frand() < 0.07f)
    for (int i = 0; i < MAX_RINGS; i++)
      if (!ringOn[i]) { ringOn[i] = true; ring[i][0] = frandR(2, W - 3); ring[i][1] = frandR(2, H - 3); ring[i][2] = 0; ring[i][3] = frand(); break; }
  cvClear();
  for (int i = 0; i < MAX_RINGS; i++) {
    if (!ringOn[i]) continue;
    ring[i][2] += 0.4f;
    float fadeOut = fmaxf(0, 1 - ring[i][2] / 16);
    for (int x = 0; x < W; x++)
      for (int y = 0; y < H; y++) {
        float d = fabsf(hypotf(x - ring[i][0], y - ring[i][1]) - ring[i][2]);
        if (d < 1.2f) cvAddHsv(x, y, ring[i][3], 1, (1.2f - d) / 1.2f * fadeOut);
      }
    if (ring[i][2] >= 16) ringOn[i] = false;
  }
}

static bool cells[W][H], cellsNew[W][H]; static uint8_t cellAge[W][H];
static int popHist[12]; static int popN; static uint32_t lifeFrames;
static void zaai() {
  for (int x = 0; x < W; x++) for (int y = 0; y < H; y++) { cells[x][y] = frand() < 0.35f; cellAge[x][y] = 0; }
  popN = 0; lifeFrames = 0;
}
static void pLeven(uint32_t s, bool first) {
  if (first) zaai();
  int pop = 0;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      int n = 0;
      for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
          if ((dx || dy) && cells[(x + dx + W) % W][(y + dy + H) % H]) n++;
      cellsNew[x][y] = (n == 3) || (cells[x][y] && n == 2);
      cellAge[x][y] = cellsNew[x][y] ? (cellAge[x][y] < 250 ? cellAge[x][y] + 1 : 250) : 0;
      if (cellsNew[x][y]) pop++;
    }
  memcpy(cells, cellsNew, sizeof(cells));
  popHist[popN % 12] = pop; popN++; lifeFrames++;
  bool vast = false;
  if (popN >= 12) {
    int a = popHist[0], b = -1; vast = true;
    for (int i = 1; i < 12; i++) { if (popHist[i] != a) { if (b < 0) b = popHist[i]; else if (popHist[i] != b) { vast = false; break; } } }
  }
  if (vast || lifeFrames > 400) zaai();
  cvClear();
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++)
      if (cells[x][y]) cvSetHsv(x, y, 0.5f + fminf(cellAge[x][y], 30) / 40.0f, 1, 1);
}

static int snakeX, snakeY; static float snakeH;
static void pSlang(uint32_t s, bool first) {
  if (first) { cvClear(); snakeX = W / 2; snakeY = H / 2; snakeH = 0; }
  cvFade(0.85f);
  switch (random(4)) { case 0: snakeX++; break; case 1: snakeX--; break; case 2: snakeY++; break; default: snakeY--; }
  snakeX = constrain(snakeX, 0, W - 1); snakeY = constrain(snakeY, 0, H - 1);
  snakeH += 0.01f;
  cvAddHsv(snakeX, snakeY, snakeH, 1, 1);
}

static void pBalken(uint32_t s, bool first) {
  float t = s * 0.3f;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      float v = (sinf((x - t) * 0.8f) + 1) / 2;
      cvSetHsv(x, y, t * 0.02f + y / 40.0f, 1, v * v * v);
    }
}

static void pAdemen(uint32_t s, bool first) {
  float t = s * 0.08f;
  float v = (sinf(t) + 1) / 2;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) cvSetHsv(x, y, t / 20, 1, 0.05f + 0.95f * v);
}

static void pRadar(uint32_t s, bool first) {
  if (first) cvClear();
  float t = s * 0.15f;
  cvFade(0.88f);
  for (int r = 0; r < 14; r++)
    cvAdd((int)lroundf(CX + r * cosf(t)), (int)lroundf(CY + r * sinf(t)), 0.2f, 1, 0.3f);
}

static const char *HART[10] = {
  "..XXX...XXX..", ".XXXXX.XXXXX.", "XXXXXXXXXXXXX", "XXXXXXXXXXXXX", ".XXXXXXXXXXX.",
  "..XXXXXXXXX..", "...XXXXXXX...", "....XXXXX....", ".....XXX.....", "......X......" };
static void pHart(uint32_t s, bool first) {
  float t = s * 0.05f, p = t - floorf(t);
  float v = (p < 0.12f || (p > 0.22f && p < 0.34f)) ? 1.0f : 0.35f;
  cvClear();
  for (int j = 0; j < 10; j++)
    for (int i = 0; i < 13; i++)
      if (HART[j][i] == 'X') cvSet(i + 3, j + 5, v, 0, v * 0.15f);
}

static void pSchaakbord(uint32_t s, bool first) {
  float t = s * 0.25f; int o = (int)t;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      if ((((x + o) / 4) + ((y + o) / 4)) % 2 == 0) cvSetHsv(x, y, t * 0.03f, 1, 0.8f);
      else cvSetHsv(x, y, t * 0.03f + 0.5f, 1, 0.25f);
    }
}

#define MAX_DROPS 40
static float rdrop[MAX_DROPS][3]; static bool rdropOn[MAX_DROPS];
static void pRegen(uint32_t s, bool first) {
  if (first) { cvClear(); for (int i = 0; i < MAX_DROPS; i++) rdropOn[i] = false; }
  cvFade(0.7f);
  if (frand() < 0.5f)
    for (int i = 0; i < MAX_DROPS; i++)
      if (!rdropOn[i]) { rdropOn[i] = true; rdrop[i][0] = random(W); rdrop[i][1] = 0; rdrop[i][2] = frandR(0.4f, 0.9f); break; }
  for (int i = 0; i < MAX_DROPS; i++) {
    if (!rdropOn[i]) continue;
    rdrop[i][1] += rdrop[i][2];
    cvAdd((int)rdrop[i][0], (int)rdrop[i][1], 0.2f, 0.4f, 1);
    if (rdrop[i][1] >= H) rdropOn[i] = false;
  }
}

static float seed[5][4];
static void pLava(uint32_t s, bool first) {
  if (first) for (int i = 0; i < 5; i++) { seed[i][0] = frandR(0, W); seed[i][1] = frandR(0, H); seed[i][2] = frandR(0.5f, 1.5f); seed[i][3] = frand() * 6; }
  float t = s * 0.12f;
  for (int x = 0; x < W; x++)
    for (int y = 0; y < H; y++) {
      float v = 0;
      for (int i = 0; i < 5; i++) v += sinf(hypotf(x - seed[i][0], y - seed[i][1]) * seed[i][2] * 0.4f - t + seed[i][3]);
      v = (v / 5 + 1) / 2;
      cvSet(x, y, v, v * 0.3f, (1 - v) * 0.6f);
    }
}

static void pKomeet(uint32_t s, bool first) {
  if (first) cvClear();
  float t = s * 0.12f;
  cvFade(0.8f);
  cvAddHsv((int)lroundf(CX + 9 * cosf(t)), (int)lroundf(CY + 9 * sinf(t * 1.3f)), t * 0.05f, 1, 1);
}

static void pConfetti(uint32_t s, bool first) {
  if (first) cvClear();
  cvFade(0.93f);
  for (int i = 0; i < 3; i++) cvAddHsv(random(W), random(H), frand(), 1, 1);
}

static void pSinus(uint32_t s, bool first) {
  if (first) cvClear();
  float t = s * 0.2f;
  cvFade(0.6f);
  for (int x = 0; x < W; x++)
    cvAddHsv(x, (int)lroundf(CY + 7 * sinf(x * 0.5f + t)), (float)x / W + t * 0.1f, 1, 1);
}

typedef void (*PatroonFn)(uint32_t, bool);
struct Patroon { const char *naam; PatroonFn fn; };
static const Patroon PATRONEN[] = {
  {"plasma", pPlasma}, {"regenboog", pRegenboog}, {"vuur", pVuur}, {"matrix", pMatrix},
  {"sterren", pSterren}, {"ballen", pBallen}, {"spiraal", pSpiraal}, {"rimpels", pRimpels},
  {"leven", pLeven}, {"slang", pSlang}, {"balken", pBalken}, {"ademen", pAdemen},
  {"radar", pRadar}, {"hart", pHart}, {"schaakbord", pSchaakbord}, {"regen", pRegen},
  {"lava", pLava}, {"komeet", pKomeet}, {"confetti", pConfetti}, {"sinus", pSinus},
};
static const int N_PATRONEN = sizeof(PATRONEN) / sizeof(PATRONEN[0]);

// ---------- animatie-besturing ----------
static bool animOn = true, autoWissel = true, eerste = true;
static int huidig = 0;
static uint32_t stap = 0, laatsteFrame = 0, patroonStart = 0;

static void kiesPatroon(int i) {
  huidig = ((i % N_PATRONEN) + N_PATRONEN) % N_PATRONEN;
  stap = 0; eerste = true; patroonStart = millis();
  Serial.printf("patroon %d: %s\n", huidig, PATRONEN[huidig].naam);
}

// ---------- USB ----------
static bool inFrame = false;
static int frameFill = 0;

void handleLine(char *line) {
  int a, b, c, d, e;
  if (line[0] == 'n') { animOn = true; kiesPatroon(huidig + 1); }
  else if (line[0] == 'k' && sscanf(line + 1, "%d", &a) == 1) { animOn = true; autoWissel = false; kiesPatroon(a); }
  else if (line[0] == 'a') { animOn = true; autoWissel = true; Serial.println("automatisch wisselen aan"); }
  else if (line[0] == 'l') { for (int i = 0; i < N_PATRONEN; i++) Serial.printf("%2d %s\n", i, PATRONEN[i].naam); }
  else if (line[0] == 'd') { animOn = true; Serial.println("animaties aan"); }
  else if (line[0] == 'f' && sscanf(line + 1, "%d %d %d", &a, &b, &c) == 3) { animOn = false; fillAll(a, b, c); show(); Serial.println("ok vul"); }
  else if (line[0] == 'p' && sscanf(line + 1, "%d %d %d %d %d", &a, &b, &c, &d, &e) == 5) { animOn = false; setPixel(a, b, c, d, e); show(); Serial.println("ok pixel"); }
  else if (line[0] == 'c') { animOn = false; clearAll(); show(); Serial.println("ok uit"); }
  else Serial.println("n | k nr | a | l | d | f r g b | p x y r g b | c");
}

void pollSerial() {
  static char buf[48];
  static int n = 0;
  while (Serial.available()) {
    uint8_t ch = (uint8_t)Serial.read();
    if (ch == 0xFF) { inFrame = true; frameFill = 0; n = 0; continue; }
    if (inFrame) {
      int i = frameFill / 3, ch_i = frameFill % 3;
      int li = ledIndex(i / H, i % H);
      pend[pendWrite][3 * li + ch_i] = clampVal(ch);
      if (++frameFill >= NUM_LEDS * 3) { inFrame = false; animOn = false; show(); }
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
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  pinMode(OUT_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)OUT_PIN, GPIO_DRIVE_CAP_3);
  stroomAan();
  cyc100ns = getCpuFrequencyMhz() / 10;
  frame[0] = 0x00;
  frame[1] = 0x01;
  memset(frame + 2, 0, NUM_LEDS * 3);
  memset(pend, 0, sizeof(pend));
  randomSeed(esp_random());
  xTaskCreatePinnedToCore(senderTask, "sender", 4096, NULL, 2, NULL, 0);
  delay(500);
  Serial.printf("led_matrix_patronen v4: %d patronen, %d s per patroon. Typ l voor de lijst.\n", N_PATRONEN, PATROON_SECONDEN);
  kiesPatroon(0);
}

void loop() {
  pollSerial();
  if (animOn) {
    uint32_t nu = millis();
    if (autoWissel && nu - patroonStart >= PATROON_SECONDEN * 1000UL) kiesPatroon(huidig + 1);
    if (nu - laatsteFrame >= FRAME_INTERVAL_MS) {
      laatsteFrame = nu;
      PATRONEN[huidig].fn(stap, eerste);
      eerste = false;
      stap++;
      cvShow();
    }
  }
  delay(1);
}
