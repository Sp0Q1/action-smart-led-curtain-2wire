// pulse_capture.ino
// ESP32 als mini-logic-analyzer voor de L1- lijn van de ledcontroller.
//
// Aansluiting:
//   ESP32 GND  -> GND-pad van de print
//   L1-        -> 2k -> GPIO 5 -> 2k -> 1k -> GND   (spanningsdeler: 5 V wordt ~3.0 V)
//   Bord: TUDCTF2025 badge (ESP32-S3). GPIO 1-4 hebben leds, dus niet gebruiken.
//
// Serial Monitor op 115200 baud. Lichtjes aan op een vaste kleur.
// Elke 5 seconden een nieuwe opname.

#include <Arduino.h>
#include "esp_timer.h"
#include "soc/gpio_reg.h"

#define CAPTURE_PIN 5        // moet een GPIO onder de 32 zijn, zonder led/knop eraan
#define N_SAMPLES   60000    // ~6 ms opname bij ~10 MHz

static uint8_t samples[N_SAMPLES];

static void addToBin(float us, int *bins) {
  if      (us < 0.25f) bins[0]++;
  else if (us < 0.6f)  bins[1]++;
  else if (us < 1.0f)  bins[2]++;
  else if (us < 5.0f)  bins[3]++;
  else if (us < 50.0f) bins[4]++;
  else                 bins[5]++;
}

void setup() {
  Serial.begin(115200);
  pinMode(CAPTURE_PIN, INPUT);
  delay(1500);
  Serial.println("Pulse capture gestart. Zet de lichtjes op een vaste kleur.");
}

void loop() {
  const uint32_t mask = 1UL << CAPTURE_PIN;

  // --- Opname: zo snel mogelijk de pin uitlezen, zonder interrupts ---
  noInterrupts();
  int64_t t0 = esp_timer_get_time();
  for (int i = 0; i < N_SAMPLES; i++) {
    samples[i] = (REG_READ(GPIO_IN_REG) & mask) ? 1 : 0;
  }
  int64_t t1 = esp_timer_get_time();
  interrupts();

  float nsPerSample = (float)(t1 - t0) * 1000.0f / N_SAMPLES;
  float usPerSample = nsPerSample / 1000.0f;

  Serial.printf("\n=== Opname: %d samples, %.0f ns per sample (%.1f MHz) ===\n",
                N_SAMPLES, nsPerSample, 1000.0f / nsPerSample);

  // --- Pulsen uitrekenen (run-length) ---
  int highBins[6] = {0, 0, 0, 0, 0, 0};
  int lowBins[6]  = {0, 0, 0, 0, 0, 0};
  int transitions = 0;
  int printed = 0;
  int i = 0;

  Serial.println("Eerste pulsen (H = hoog, L = laag), duur in microseconden:");
  while (i < N_SAMPLES) {
    uint8_t v = samples[i];
    int run = 0;
    while (i < N_SAMPLES && samples[i] == v) { run++; i++; }
    float us = run * usPerSample;
    if (v) addToBin(us, highBins); else addToBin(us, lowBins);
    transitions++;
    if (printed < 120) {
      Serial.printf("%c %.2f   ", v ? 'H' : 'L', us);
      if (++printed % 6 == 0) Serial.println();
    }
  }
  Serial.println();
  Serial.printf("Aantal overgangen in de opname: %d\n", transitions);

  const char *labels[6] = {"<0.25 us", "0.25-0.6 us", "0.6-1.0 us", "1-5 us", "5-50 us", ">50 us"};
  Serial.println("Histogram HOOG-pulsen:");
  for (int b = 0; b < 6; b++) Serial.printf("  %-12s %d\n", labels[b], highBins[b]);
  Serial.println("Histogram LAAG-pulsen:");
  for (int b = 0; b < 6; b++) Serial.printf("  %-12s %d\n", labels[b], lowBins[b]);

  Serial.println("Lezen: WS281x-achtig = veel pulsen in 0.25-0.6 en 0.6-1.0,");
  Serial.println("       plus af en toe een lange pauze (>50 us) als reset.");
  Serial.println("       Alleen H of alleen L = spanningsdeler aanpassen of verkeerde pin.");

  delay(5000);
}
