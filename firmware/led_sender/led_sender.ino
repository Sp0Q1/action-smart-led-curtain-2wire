// led_sender.ino  (v1)
// Stuurt zelf het 2-draads protocol van het ledsnoer.
//
// Protocol (uit de opnames):
//   frame = 00 01, daarna per led 3 bytes R G B (max 30), daarna 1 los bit,
//   daarna minstens 40 us rust met de stroom aan. Frames bijna aaneengesloten
//   zenden (FRAME_MS 1): met lange pauzes flikkeren de leds.
//   bit 0 = stroom 2,5 us uit + 2,8 us aan
//   bit 1 = stroom 4,5 us uit + 5,9 us aan
//
// Aansluiting (low-side schakeltrap, badge = ESP32-S3):
//   GPIO 6 -> 100 ohm -> gate N-MOSFET      (NPN: 220 ohm -> basis)
//   gate -> 10k -> GND
//   source (emitter) -> GND  = min-rail = GND print = GND badge
//   drain (collector) -> L1- draad van het LEDSNOER (losgeknipt van de print)
//   L+ van het snoer blijft op de print; de print levert de 5 V.
//
// INVERT 1 als jouw trap omkeert (pin HOOG = stroom UIT).
// TRAIL_BIT: het losse bit aan het eind; 0 proberen, anders 1.

#include <Arduino.h>
#include "soc/gpio_reg.h"
#include "driver/gpio.h"

#define OUT_PIN    6
#define NUM_LEDS   400
#define MAX_VAL    10      // stock controller uses up to 30; keep low with a small NPN transistor
#define INVERT     0
#define TRAIL_BIT  0
#define FRAME_MS   1       // rust tussen frames; groter geeft flikkering

#define T0_OFF 27          // pulsbreedtes in 0,1 us (iets langer dan gemeten: transistor gaat traag uit)
#define T0_ON  28
#define T1_OFF 50
#define T1_ON  59

static uint8_t frame[2 + NUM_LEDS * 3];
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

void sendFrame() {
  noInterrupts();
  uint32_t t = ESP.getCycleCount();
  for (size_t i = 0; i < sizeof(frame); i++) {
    uint8_t b = frame[i];
    for (int k = 7; k >= 0; k--) sendBit((b >> k) & 1, t);
  }
  sendBit(TRAIL_BIT, t);
  interrupts();
}

void setLed(int i, uint8_t r, uint8_t g, uint8_t b) {
  if (i < 0 || i >= NUM_LEDS) return;
  frame[2 + 3 * i]     = r > MAX_VAL ? MAX_VAL : r;
  frame[2 + 3 * i + 1] = g > MAX_VAL ? MAX_VAL : g;
  frame[2 + 3 * i + 2] = b > MAX_VAL ? MAX_VAL : b;
}

void fillAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_LEDS; i++) setLed(i, r, g, b);
}

void showFor(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    sendFrame();
    delay(FRAME_MS);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(OUT_PIN, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)OUT_PIN, GPIO_DRIVE_CAP_3);
  stroomAan();                          // in rust: stroom aan
  cyc100ns = getCpuFrequencyMhz() / 10;
  frame[0] = 0x00;
  frame[1] = 0x01;
  fillAll(0, 0, 0);
  delay(1000);
  Serial.printf("LED sender: %d leds, %u cycles per 0.1 us, INVERT=%d, TRAIL_BIT=%d\n",
                NUM_LEDS, cyc100ns, INVERT, TRAIL_BIT);
}

void loop() {
  Serial.println("Alles rood");
  fillAll(MAX_VAL, 0, 0);
  showFor(2000);

  Serial.println("Alles groen");
  fillAll(0, MAX_VAL, 0);
  showFor(2000);

  Serial.println("Alles blauw");
  fillAll(0, 0, MAX_VAL);
  showFor(2000);

  Serial.println("Alles wit (15/10/15)");
  fillAll(15, 10, 15);
  showFor(2000);

  Serial.println("Lopend blokje");
  for (int pos = 0; pos < NUM_LEDS; pos += 4) {
    fillAll(0, 0, 0);
    for (int k = 0; k < 10; k++) setLed(pos + k, MAX_VAL, 0, MAX_VAL / 3);
    sendFrame();
    delay(FRAME_MS);
  }

  Serial.println("Uit");
  fillAll(0, 0, 0);
  showFor(1000);
}
