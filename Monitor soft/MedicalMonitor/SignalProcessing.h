#pragma once
#include <stdint.h>
#include <math.h>
#include "EcgProcessing.h"
#include "Pulse+SpO2/MAX30102_by_RF-master/algorithm_by_RF.h"

// All timestamps are acquisition times, never display/SD write times.
struct PulseOxProcessor {
  uint32_t ir[BUFFER_SIZE] = {}, red[BUFFER_SIZE] = {};
  int count = 0, step = 0, goodWindows = 0;
  int beatAvg = 0;
  float spo2 = 0, correlation = 0, periodicity = 0, perfusion = 0;
  bool finger = false, hrValid = false, spo2Valid = false;
  uint32_t lastSample = 0;

  void reset() {
    count = step = goodWindows = beatAvg = 0;
    spo2 = correlation = periodicity = perfusion = 0;
    finger = hrValid = spo2Valid = false;
    lastSample = 0;
    rf_reset_state();
  }
  void expire(uint32_t now) { if (lastSample && now - lastSample > 300) reset(); }
  void update(uint32_t irValue, uint32_t redValue, uint32_t now) {
    if (lastSample && now - lastSample > 60) reset();
    if (irValue < 50000 || redValue < 1000 || irValue >= 260000 || redValue >= 260000) {
      reset();
      lastSample = now;
      return;
    }
    finger = true;
    lastSample = now;
    if (count == BUFFER_SIZE) {
      for (int i = 1; i < BUFFER_SIZE; ++i) { ir[i-1] = ir[i]; red[i-1] = red[i]; }
      --count;
    }
    ir[count] = irValue; red[count++] = redValue;
    if (count < BUFFER_SIZE) return;
    if (step > 0 && --step > 0) return;
    step = FS;
    int32_t hr = 0;
    int8_t hv = 0, sv = 0;
    float oxygen = 0;
    rf_heart_rate_and_oxygen_saturation(ir, BUFFER_SIZE, red, &oxygen, &sv, &hr, &hv,
                                      &periodicity, &correlation);
    double mean = 0, variance = 0;
    for (int i = 0; i < BUFFER_SIZE; ++i) mean += ir[i];
    mean /= BUFFER_SIZE;
    for (int i = 0; i < BUFFER_SIZE; ++i) { double d = ir[i] - mean; variance += d*d; }
    perfusion = float(100 * sqrt(variance / BUFFER_SIZE) / mean);
    // Reject very weak modulation. These are engineering gates, not accuracy claims.
    if (!hv || !isfinite(correlation) || perfusion < 0.1f) {
      goodWindows = 0; hrValid = spo2Valid = false; beatAvg = 0; spo2 = 0;
      return;
    }
    if (goodWindows < 2) ++goodWindows;
    hrValid = goodWindows >= 2;
    spo2Valid = hrValid && sv;
    beatAvg = hrValid ? hr : 0;
    spo2 = spo2Valid ? oxygen : 0;
  }
};

struct DebouncedButton {
  static constexpr uint32_t PRESS_MS = 20;
  static constexpr uint32_t RELEASE_MS = 25;
  bool raw = false, stable = false, repeatArmed = false;
  uint32_t changed = 0, pressed = 0, repeated = 0;
  bool update(bool value, uint32_t now, bool available = true, bool repeat = false) {
    if (!available) {
      raw = stable; changed = now; repeatArmed = false; return false;
    }
    if (!repeat) repeatArmed = false;
    if (value != raw) { raw = value; changed = now; }
    if (raw != stable && uint32_t(now-changed) >= (raw ? PRESS_MS : RELEASE_MS)) {
      stable = raw;
      if (stable) {
        pressed = repeated = now; repeatArmed = repeat; return true;
      }
      repeatArmed = false;
    }
    // Never repeat through a release glitch, a screen change or an ADC error.
    if (repeatArmed && repeat && raw && stable) {
      uint32_t held = now-pressed;
      uint32_t interval = held<1500 ? 220 : held<3000 ? 120 : 60;
      if (held>=500 && uint32_t(now-repeated)>=interval) {
        repeated=now;return true; // at most one event, no catch-up burst
      }
    }
    return false;
  }
};

inline int estimateBatteryPercent(float v) {
  // Approximate rested single-cell Li-ion curve, not a fuel gauge.
  const float volts[] = {3.3f, 3.5f, 3.65f, 3.7f, 3.75f, 3.8f, 3.9f, 4.0f, 4.1f, 4.2f};
  const int pct[] = {0, 5, 15, 25, 40, 55, 70, 85, 95, 100};
  if (!isfinite(v) || v < 2.5f || v > 4.5f) return -1;
  if (v <= volts[0]) return 0;
  for (int i = 1; i < 10; ++i)
    if (v < volts[i]) return pct[i-1] + int((v-volts[i-1])/(volts[i]-volts[i-1])*(pct[i]-pct[i-1]));
  return 100;
}
