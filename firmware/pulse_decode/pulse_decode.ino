// pulse_decode.ino  (v2)
// Neemt een lange reeks overgangen op L1- op, zet korte H-pulsen om in 0
// en lange H-pulsen in 1, knipt op pauzes in frames en print hex-bytes.
//
// Aansluiting (zelfde als v1):
//   L1-  -> 2k -> GPIO 5 -> 2k -> 1k -> GND   (deler, 5 V wordt ~3 V)
//   GND van de badge aan de min-rail, samen met GND van de print.
//
// Serial Monitor 115200. Elke 10 seconden een nieuwe opname.

#include <Arduino.h>
#include "esp_timer.h"
#include "soc/gpio_reg.h"

#define CAPTURE_PIN     5
#define MAX_RUNS        80000        // aantal pulsen per opname (~40000 bits)
#define MAX_SAMPLES     20000000UL   // noodstop: ~2 s bij 10 MHz
#define SHORT_LONG_US   3.5f         // H korter = 0, langer = 1
#define GAP_US          20.0f        // L langer dan dit = pauze tussen frames
#define MAX_PRINT_BYTES 160          // per frame niet meer printen dan dit

static uint16_t runs[MAX_RUNS];

void setup() {
  Serial.begin(115200);
  pinMode(CAPTURE_PIN, INPUT);
  delay(1500);
  Serial.println("Pulse decode v2. Lichtjes op een vaste kleur zetten. Elke 10 s een opname.");
}

void loop() {
  const uint32_t mask = 1UL << CAPTURE_PIN;

  // --- Opname van overgangen, zo snel mogelijk, zonder interrupts ---
  noInterrupts();
  int64_t t0 = esp_timer_get_time();
  uint32_t last = REG_READ(GPIO_IN_REG) & mask;
  uint8_t firstLevel = last ? 1 : 0;
  uint32_t run = 0, total = 0;
  int n = 0;
  while (n < MAX_RUNS && total < MAX_SAMPLES) {
    uint32_t v = REG_READ(GPIO_IN_REG) & mask;
    if (v != last) {
      runs[n++] = (run > 65535) ? 65535 : run;
      run = 0;
      last = v;
    }
    run++;
    total++;
  }
  int64_t t1 = esp_timer_get_time();
  interrupts();

  float usPerSample = (float)(t1 - t0) / (float)total;
  Serial.printf("\n=== %d overgangen in %.1f ms, %.3f us per sample ===\n",
                n, (t1 - t0) / 1000.0f, usPerSample);
  if (n < 10) {
    Serial.println("Bijna geen signaal. Aansluiting en kleurstand checken.");
    delay(10000);
    return;
  }

  // --- Statistiek van de H-pulsen (= stroom even uit) ---
  int hShort = 0, hLong = 0, hOther = 0, gaps = 0;
  int hHist[40];
  for (int b = 0; b < 40; b++) hHist[b] = 0;
  uint8_t lvl = firstLevel;
  for (int i = 0; i < n; i++) {
    float us = runs[i] * usPerSample;
    if (lvl) {
      int b = (int)(us / 0.25f);
      if (b > 39) b = 39;
      hHist[b]++;
      if (us < 1.0f)               hOther++;
      else if (us < SHORT_LONG_US) hShort++;
      else if (us < 8.0f)          hLong++;
      else                         hOther++;
    } else if (us > GAP_US) {
      gaps++;
    }
    lvl ^= 1;
  }
  Serial.printf("H-pulsen: kort=%d lang=%d overig=%d | pauzes (L > %.0f us): %d\n",
                hShort, hLong, hOther, GAP_US, gaps);
  Serial.println("H-breedte histogram (vakjes van 0.25 us):");
  for (int b = 0; b < 40; b++) {
    if (hHist[b]) Serial.printf("  %.2f-%.2f us: %d\n", b * 0.25f, (b + 1) * 0.25f, hHist[b]);
  }

  // --- Decoderen: kort = 0, lang = 1, pauze = framegrens ---
  Serial.println("Bytes per frame (kort=0, lang=1, eerste bit = hoogste bit):");
  lvl = firstLevel;
  uint8_t acc = 0;
  int bitCnt = 0, frameNo = 0, bytesOnLine = 0, bytesPrinted = 0;
  long frameBits = 0;
  bool inFrame = false;
  for (int i = 0; i < n; i++) {
    float us = runs[i] * usPerSample;
    if (lvl) {
      if (!inFrame) {
        inFrame = true;
        frameNo++;
        frameBits = 0; acc = 0; bitCnt = 0; bytesOnLine = 0; bytesPrinted = 0;
        Serial.printf("\n-- frame %d --\n", frameNo);
      }
      int bit = (us >= SHORT_LONG_US) ? 1 : 0;
      acc = (acc << 1) | bit;
      bitCnt++;
      frameBits++;
      if (bitCnt == 8) {
        if (bytesPrinted < MAX_PRINT_BYTES) {
          Serial.printf("%02X ", acc);
          bytesPrinted++;
          if (++bytesOnLine == 24) { Serial.println(); bytesOnLine = 0; }
        } else if (bytesPrinted == MAX_PRINT_BYTES) {
          Serial.print("... ");
          bytesPrinted++;
        }
        acc = 0; bitCnt = 0;
      }
    } else if (us > GAP_US && inFrame) {
      if (bitCnt) Serial.printf("(+%d losse bits) ", bitCnt);
      Serial.printf("\n   einde frame %d: %ld bits, daarna pauze van %.1f us\n", frameNo, frameBits, us);
      inFrame = false;
    }
    lvl ^= 1;
  }
  if (inFrame) {
    Serial.printf("\n   (frame %d loopt nog door: %ld bits tot het einde van de opname)\n", frameNo, frameBits);
  }
  Serial.println("\nKlaar. Kleur veranderen en de volgende opname afwachten.");
  delay(10000);
}
