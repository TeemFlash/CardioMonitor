#pragma once
#include <stdint.h>
#include <math.h>
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

struct EcgBeatDetector {
  float baseline = 0, filtered = 0, envelope = 20, bpm = 0;
  uint32_t lastBeat = 0, lastSample = 0, start = 0;
  int intervals = 0;
  bool initialized = false, armed = true, quality = false;
  void reset() { *this = EcgBeatDetector(); }
  void expire(uint32_t now) {
    if (lastSample && now - lastSample > 100) reset();
    if (lastBeat && now - lastBeat > 2500) { bpm = 0; intervals = 0; }
  }
  void update(int raw, uint32_t now) {
    if (raw < 150 || raw > 3950) { reset(); return; }
    if (lastSample && now - lastSample > 20) reset();
    if (!initialized) { baseline = raw; start = now; initialized = true; }
    lastSample = now;
    // 250 Hz: baseline rejection ~0.5 Hz, low-pass ~35 Hz. Display is relative.
    baseline += 0.0124f * (raw - baseline);
    filtered += 0.585f * ((raw - baseline) - filtered);
    float amplitude = fabsf(filtered);
    envelope = fmaxf(amplitude, envelope * 0.996f);
    quality = now - start > 2000 && envelope > 12;
    if (!quality) { bpm = 0; return; }
    float threshold = fmaxf(12, envelope * 0.6f);
    if (armed && amplitude > threshold && (!lastBeat || now - lastBeat > 300)) {
      if (lastBeat) {
        float rate = 60000.0f / (now - lastBeat);
        if (rate >= 40 && rate <= 200) {
          ++intervals;
          if (intervals >= 2) bpm = bpm > 0 ? bpm * 0.7f + rate * 0.3f : rate;
        } else { intervals = 0; bpm = 0; }
      }
      lastBeat = now; armed = false;
    }
    if (amplitude < threshold * 0.45f) armed = true;
    expire(now);
  }
};

struct DebouncedButton {
  bool raw = false, stable = false;
  uint32_t changed = 0, pressed = 0, repeated = 0;
  bool update(bool value, uint32_t now, bool repeat = false) {
    if (value != raw) { raw = value; changed = now; }
    if (raw != stable && now - changed >= 35) {
      stable = raw;
      if (stable) { pressed = repeated = now; return true; }
    }
    if (repeat && stable && now - pressed >= 600 && now - repeated >= 110) {
      repeated = now; return true;
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
